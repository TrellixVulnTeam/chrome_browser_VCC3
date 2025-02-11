#!/usr/bin/env python
#
# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Integration test for breakpad in content shell.

This test checks that content shell and breakpad are correctly hooked up, as
well as that the tools can symbolize a stack trace."""


import glob
import json
import optparse
import os
import shutil
import subprocess
import sys
import tempfile
import time


CONCURRENT_TASKS=4
BREAKPAD_TOOLS_DIR = os.path.join(
  os.path.dirname(__file__), '..', '..', '..',
  'components', 'crash', 'content', 'tools')
ANDROID_CRASH_DIR = '/data/local/tmp/crashes'

def build_is_android(build_dir):
  return os.path.isfile(os.path.join(build_dir, 'bin', 'content_shell_apk'))

def android_crash_dir_exists():
  proc = subprocess.Popen(['adb', 'shell', 'ls', ANDROID_CRASH_DIR],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
  [stdout, stderr] = proc.communicate()
  not_found = 'No such file or directory'
  return not_found not in stdout and not_found not in stderr

def get_android_dump(crash_dir):
  global failure

  pending = ANDROID_CRASH_DIR + '/pending/'

  for attempts in range(5):
    proc = subprocess.Popen(
        ['adb', 'shell', 'ls', pending + '*.dmp'],
    stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    dumps = [f for f in map(str.strip, proc.communicate()[0].split('\n'))
             if f.endswith('.dmp')]
    if len(dumps) > 0:
      break
    # Crashpad may still be writing the dump. Sleep and try again.
    time.sleep(1)

  if len(dumps) != 1:
    failure = 'Expected 1 crash dump, found %d.' % len(dumps)
    print dumps
    raise Exception(failure)

  subprocess.check_call(['adb', 'pull', dumps[0], crash_dir])
  subprocess.check_call(['adb', 'shell', 'rm', pending + '*'])

  return os.path.join(crash_dir, os.path.basename(dumps[0]))

def run_test(options, crash_dir, symbols_dir, platform,
             additional_arguments = []):
  global failure

  print '# Run content_shell and make it crash.'
  if platform == 'android':
    cmd = [os.path.join(options.build_dir, 'bin', 'content_shell_apk'),
           'launch',
           'chrome://crash',
           '--args=--enable-crash-reporter --crash-dumps-dir=%s ' %
           ANDROID_CRASH_DIR + ' '.join(additional_arguments)]
  else:
    cmd = [options.binary,
           '--run-web-tests',
           'chrome://crash',
           '--enable-crash-reporter',
           '--crash-dumps-dir=%s' % crash_dir]
    cmd += additional_arguments

  if options.verbose:
    print ' '.join(cmd)
  failure = 'Failed to run content_shell.'
  if options.verbose:
    subprocess.check_call(cmd)
  else:
    # On Windows, using os.devnull can cause check_call to never return,
    # so use a temporary file for the output.
    with tempfile.TemporaryFile() as tmpfile:
      subprocess.check_call(cmd, stdout=tmpfile, stderr=tmpfile)

  print '# Retrieve crash dump.'
  if platform == 'android':
    dmp_file = get_android_dump(crash_dir)
  else:
    dmp_dir = crash_dir
    # TODO(crbug.com/782923): This test should not reach directly into the
    # Crashpad database, but instead should use crashpad_database_util.
    if platform == 'darwin':
      dmp_dir = os.path.join(dmp_dir, 'pending')
    elif platform == 'win32':
      dmp_dir = os.path.join(dmp_dir, 'reports')

    dmp_files = glob.glob(os.path.join(dmp_dir, '*.dmp'))
    failure = 'Expected 1 crash dump, found %d.' % len(dmp_files)
    if len(dmp_files) != 1:
      raise Exception(failure)
    dmp_file = dmp_files[0]

  if platform not in ('darwin', 'win32', 'android'):
    minidump = os.path.join(crash_dir, 'minidump')
    dmp_to_minidump = os.path.join(BREAKPAD_TOOLS_DIR, 'dmp2minidump.py')
    cmd = [dmp_to_minidump, dmp_file, minidump]
    if options.verbose:
      print ' '.join(cmd)
    failure = 'Failed to run dmp_to_minidump.'
    subprocess.check_call(cmd)
  else:
    minidump = dmp_file

  print '# Symbolize crash dump.'
  if platform == 'win32':
    cdb_exe = os.path.join(options.build_dir, 'cdb', 'cdb.exe')
    cmd = [cdb_exe, '-y', options.build_dir, '-c', '.lines;.excr;k30;q',
           '-z', dmp_file]
    if options.verbose:
      print ' '.join(cmd)
    failure = 'Failed to run cdb.exe.'
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    stack = proc.communicate()[0]
  else:
    minidump_stackwalk = os.path.join(options.build_dir, 'minidump_stackwalk')
    cmd = [minidump_stackwalk, minidump, symbols_dir]
    if options.verbose:
      print ' '.join(cmd)
    failure = 'Failed to run minidump_stackwalk.'
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    stack = proc.communicate()[0]

  # Check whether the stack contains a CrashIntentionally symbol.
  found_symbol = 'CrashIntentionally' in stack

  os.remove(dmp_file)

  if options.no_symbols:
    if found_symbol:
      if options.verbose:
        print stack
      failure = 'Found unexpected reference to CrashIntentionally in stack'
      raise Exception(failure)
  else:
    if not found_symbol:
      if options.verbose:
        print stack
      failure = 'Could not find reference to CrashIntentionally in stack.'
      raise Exception(failure)

def main():
  global failure

  parser = optparse.OptionParser()
  parser.add_option('', '--build-dir', default='',
                    help='The build output directory.')
  parser.add_option('', '--binary', default='',
                    help='The path of the binary to generate symbols for and '
                         'then run for the test.')
  parser.add_option('', '--additional-binary', default='',
                    help='An additional binary for which to generate symbols. '
                         'On Mac this is used for specifying the '
                         '"Content Shell Framework" library, which is not '
                         'linked into --binary.')
  parser.add_option('', '--no-symbols', default=False, action='store_true',
                    help='Symbols are not expected to work.')
  parser.add_option('-j', '--jobs', default=CONCURRENT_TASKS, action='store',
                    type='int', help='Number of parallel tasks to run.')
  parser.add_option('-v', '--verbose', action='store_true',
                    help='Print verbose status output.')
  parser.add_option('', '--json', default='',
                    help='Path to JSON output.')

  (options, _) = parser.parse_args()

  if not options.build_dir:
    print 'Required option --build-dir missing.'
    return 1

  if not options.binary:
    print 'Required option --binary missing.'
    return 1

  if not os.access(options.binary, os.X_OK):
    print 'Cannot find %s.' % options.binary
    return 1

  failure = ''

  if build_is_android(options.build_dir):
    platform = 'android'
    if android_crash_dir_exists():
      print 'Android crash dir exists %s' % ANDROID_CRASH_DIR
      return 1

    subprocess.check_call(['adb', 'shell', 'mkdir', ANDROID_CRASH_DIR])

  else:
    platform = sys.platform

  # Create a temporary directory to store the crash dumps and symbols in.
  crash_dir = tempfile.mkdtemp()
  symbols_dir = os.path.join(crash_dir, 'symbols')

  crash_service = None

  try:
    if platform != 'win32':
      print '# Generate symbols.'
      bins = [options.binary]
      if options.additional_binary:
        bins.append(options.additional_binary)
      generate_symbols = os.path.join(
          BREAKPAD_TOOLS_DIR, 'generate_breakpad_symbols.py')
      for binary in bins:
        cmd = [generate_symbols,
               '--build-dir=%s' % options.build_dir,
               '--binary=%s' % binary,
               '--symbols-dir=%s' % symbols_dir,
               '--jobs=%d' % options.jobs]
        if options.verbose:
          cmd.append('--verbose')
          print ' '.join(cmd)
        failure = 'Failed to run generate_breakpad_symbols.py.'
        subprocess.check_call(cmd)

    run_test(options, crash_dir, symbols_dir, platform)

  except:
    print 'FAIL: %s' % failure
    if options.json:
      with open(options.json, 'w') as json_file:
        json.dump([failure], json_file)

    return 1

  else:
    print 'PASS: Breakpad integration test ran successfully.'
    if options.json:
      with open(options.json, 'w') as json_file:
        json.dump([], json_file)
    return 0

  finally:
    if crash_service:
      crash_service.terminate()
      crash_service.wait()
    try:
      shutil.rmtree(crash_dir)
    except:
      print 'Failed to delete temp directory "%s".' % crash_dir
    if platform == 'android':
      try:
        subprocess.check_call(['adb', 'shell', 'rm', '-rf', ANDROID_CRASH_DIR])
      except:
        print 'Failed to delete android crash dir %s' % ANDROID_CRASH_DIR


if '__main__' == __name__:
  sys.exit(main())
