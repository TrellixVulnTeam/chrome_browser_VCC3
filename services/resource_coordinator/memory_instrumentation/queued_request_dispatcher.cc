// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/resource_coordinator/memory_instrumentation/queued_request_dispatcher.h"

#include <inttypes.h>

#include "base/android/library_loader/anchor_functions_buildflags.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/format_macros.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "base/process/process_metrics.h"
#include "base/strings/pattern.h"
#include "base/strings/stringprintf.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "services/resource_coordinator/memory_instrumentation/graph_processor.h"
#include "services/resource_coordinator/memory_instrumentation/switches.h"

#if defined(OS_MACOSX) && !defined(OS_IOS)
#include "base/mac/mac_util.h"
#endif

using base::trace_event::MemoryAllocatorDump;
using base::trace_event::MemoryDumpLevelOfDetail;
using base::trace_event::TracedValue;
using Node = memory_instrumentation::GlobalDumpGraph::Node;

namespace memory_instrumentation {

namespace {

// Returns the private memory footprint calculated from given |os_dump|.
//
// See design docs linked in the bugs for the rationale of the computation:
// - Linux/Android: https://crbug.com/707019 .
// - Mac OS: https://crbug.com/707021 .
// - Win: https://crbug.com/707022 .
uint32_t CalculatePrivateFootprintKb(const mojom::RawOSMemDump& os_dump,
                                     uint32_t shared_resident_kb) {
  DCHECK(os_dump.platform_private_footprint);
#if defined(OS_LINUX) || defined(OS_ANDROID)
  uint64_t rss_anon_bytes = os_dump.platform_private_footprint->rss_anon_bytes;
  uint64_t vm_swap_bytes = os_dump.platform_private_footprint->vm_swap_bytes;
  return (rss_anon_bytes + vm_swap_bytes) / 1024;
#elif defined(OS_MACOSX)
  if (base::mac::IsAtLeastOS10_12()) {
    uint64_t phys_footprint_bytes =
        os_dump.platform_private_footprint->phys_footprint_bytes;
    return base::saturated_cast<uint32_t>(
        base::saturated_cast<int32_t>(phys_footprint_bytes / 1024) -
        base::saturated_cast<int32_t>(shared_resident_kb));
  } else {
    uint64_t internal_bytes =
        os_dump.platform_private_footprint->internal_bytes;
    uint64_t compressed_bytes =
        os_dump.platform_private_footprint->compressed_bytes;
    return base::saturated_cast<uint32_t>(
        base::saturated_cast<int32_t>((internal_bytes + compressed_bytes) /
                                      1024) -
        base::saturated_cast<int32_t>(shared_resident_kb));
  }
#elif defined(OS_WIN)
  return base::saturated_cast<int32_t>(
      os_dump.platform_private_footprint->private_bytes / 1024);
#else
  return 0;
#endif
}

#if BUILDFLAG(SUPPORTS_CODE_ORDERING)
void LogNativeCodeResidentPages(const std::set<size_t>& accessed_pages_set) {
  // |SUPPORTS_CODE_ORDERING| can only be enabled on Android.
  const auto kResidentPagesPath = base::FilePath(
      "/data/local/tmp/chrome/native-library-resident-pages.txt");

  auto file = base::File(kResidentPagesPath, base::File::FLAG_CREATE_ALWAYS |
                                                 base::File::FLAG_WRITE);

  if (!file.IsValid()) {
    DLOG(ERROR) << "Could not open " << kResidentPagesPath;
    return;
  }

  for (size_t page : accessed_pages_set) {
    std::string page_str = base::StringPrintf("%" PRIuS "\n", page);

    if (file.WriteAtCurrentPos(page_str.c_str(),
                               static_cast<int>(page_str.size())) < 0) {
      DLOG(WARNING) << "Error while dumping Resident pages";
      return;
    }
  }
}

size_t ReportGlobalNativeCodeResidentMemoryKb(
    const std::map<base::ProcessId, mojom::RawOSMemDump*>& pid_to_pmd) {
  std::vector<uint8_t> common_map;

  for (const auto& pmd : pid_to_pmd) {
    if (!pmd.second || pmd.second->native_library_pages_bitmap.empty()) {
      DLOG(WARNING) << "No process pagemap entry for " << pmd.first;
      return 0;
    }

    if (common_map.size() < pmd.second->native_library_pages_bitmap.size()) {
      common_map.resize(pmd.second->native_library_pages_bitmap.size());
    }
    for (size_t i = 0; i < pmd.second->native_library_pages_bitmap.size();
         ++i) {
      common_map[i] |= pmd.second->native_library_pages_bitmap[i];
    }
  }

  // |accessed_pages_set| will be ~40kB on 32 bit mode and ~80kB on 64 bit mode.
  std::set<size_t> accessed_pages_set;
  for (size_t i = 0; i < common_map.size(); i++) {
    for (int j = 0; j < 8; j++) {
      if (common_map[i] & (1 << j))
        accessed_pages_set.insert(i * 8 + j);
    }
  }

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          "log-native-library-residency")) {
    LogNativeCodeResidentPages(accessed_pages_set);
  }

  const size_t kPageSize = base::GetPageSize();
  const size_t native_resident_bytes = accessed_pages_set.size() * kPageSize;
  // TODO(crbug.com/956464) replace adding |NativeCodeResidentMemory| to trace
  // this way by adding it through |tracing_observer| in Finalize().
  TRACE_EVENT_INSTANT1(base::trace_event::MemoryDumpManager::kTraceCategory,
                       "ReportGlobalNativeCodeResidentMemoryKb",
                       TRACE_EVENT_SCOPE_GLOBAL, "NativeCodeResidentMemory",
                       native_resident_bytes);
  return native_resident_bytes / 1024;
}
#endif  // #if BUILDFLAG(SUPPORTS_CODE_ORDERING)

memory_instrumentation::mojom::OSMemDumpPtr CreatePublicOSDump(
    const mojom::RawOSMemDump& internal_os_dump,
    uint32_t shared_resident_kb) {
  mojom::OSMemDumpPtr os_dump = mojom::OSMemDump::New();

  os_dump->resident_set_kb = internal_os_dump.resident_set_kb;
  os_dump->private_footprint_kb =
      CalculatePrivateFootprintKb(internal_os_dump, shared_resident_kb);
#if defined(OS_LINUX) || defined(OS_ANDROID)
  os_dump->private_footprint_swap_kb =
      internal_os_dump.platform_private_footprint->vm_swap_bytes / 1024;
#endif
  return os_dump;
}

void NodeAsValueIntoRecursively(const GlobalDumpGraph::Node& node,
                                TracedValue* value,
                                std::vector<base::StringPiece>* path) {
  // Don't dump the root node.
  if (!path->empty()) {
    std::string string_conversion_buffer;

    std::string name = base::JoinString(*path, "/");
    value->BeginDictionaryWithCopiedName(name);

    if (!node.guid().empty())
      value->SetString("guid", node.guid().ToString());

    value->BeginDictionary("attrs");
    for (const auto& name_to_entry : node.const_entries()) {
      const auto& entry = name_to_entry.second;
      value->BeginDictionaryWithCopiedName(name_to_entry.first);
      switch (entry.type) {
        case GlobalDumpGraph::Node::Entry::kUInt64:
          base::SStringPrintf(&string_conversion_buffer, "%" PRIx64,
                              entry.value_uint64);
          value->SetString("type", MemoryAllocatorDump::kTypeScalar);
          value->SetString("value", string_conversion_buffer);
          break;
        case GlobalDumpGraph::Node::Entry::kString:
          value->SetString("type", MemoryAllocatorDump::kTypeString);
          value->SetString("value", entry.value_string);
          break;
      }
      switch (entry.units) {
        case GlobalDumpGraph::Node::Entry::ScalarUnits::kBytes:
          value->SetString("units", MemoryAllocatorDump::kUnitsBytes);
          break;
        case GlobalDumpGraph::Node::Entry::ScalarUnits::kObjects:
          value->SetString("units", MemoryAllocatorDump::kUnitsObjects);
          break;
      }
      value->EndDictionary();
    }
    value->EndDictionary();  // "attrs": { ... }

    if (node.is_weak())
      value->SetInteger("flags", MemoryAllocatorDump::Flags::WEAK);

    value->EndDictionary();  // "allocator_name/heap_subheap": { ... }
  }

  for (const auto& name_to_child : node.const_children()) {
    path->push_back(name_to_child.first);
    NodeAsValueIntoRecursively(*name_to_child.second, value, path);
    path->pop_back();
  }
}

std::unique_ptr<TracedValue> GetChromeDumpTracedValue(
    const GlobalDumpGraph::Process& process) {
  std::unique_ptr<TracedValue> traced_value = std::make_unique<TracedValue>();
  if (!process.root()->const_children().empty()) {
    traced_value->BeginDictionary("allocators");
    std::vector<base::StringPiece> path;
    NodeAsValueIntoRecursively(*process.root(), traced_value.get(), &path);
    traced_value->EndDictionary();
  }
  return traced_value;
}

std::unique_ptr<TracedValue> GetChromeDumpAndGlobalAndEdgesTracedValue(
    const GlobalDumpGraph::Process& process,
    const GlobalDumpGraph::Process& global_process,
    const std::forward_list<GlobalDumpGraph::Edge>& edges) {
  std::unique_ptr<TracedValue> traced_value = std::make_unique<TracedValue>();
  bool suppress_graphs = process.root()->const_children().empty() &&
                         global_process.root()->const_children().empty();

  if (!suppress_graphs) {
    traced_value->BeginDictionary("allocators");
    std::vector<base::StringPiece> path;
    NodeAsValueIntoRecursively(*process.root(), traced_value.get(), &path);
    NodeAsValueIntoRecursively(*global_process.root(), traced_value.get(),
                               &path);
    traced_value->EndDictionary();
  }
  traced_value->BeginArray("allocators_graph");
  for (const auto& edge : edges) {
    traced_value->BeginDictionary();
    traced_value->SetString("source", edge.source()->guid().ToString());
    traced_value->SetString("target", edge.target()->guid().ToString());
    traced_value->SetInteger("importance", edge.priority());
    traced_value->EndDictionary();
  }
  traced_value->EndArray();
  return traced_value;
}

}  // namespace

// static
void QueuedRequestDispatcher::SetUpAndDispatch(
    QueuedRequest* request,
    const std::vector<ClientInfo>& clients,
    const ChromeCallback& chrome_callback,
    const OsCallback& os_callback) {
  using ResponseType = QueuedRequest::PendingResponse::Type;
  DCHECK(!request->dump_in_progress);
  request->dump_in_progress = true;

  request->start_time = base::TimeTicks::Now();

  TRACE_EVENT_NESTABLE_ASYNC_BEGIN2(
      base::trace_event::MemoryDumpManager::kTraceCategory, "GlobalMemoryDump",
      TRACE_ID_LOCAL(request->dump_guid), "dump_type",
      base::trace_event::MemoryDumpTypeToString(request->args.dump_type),
      "level_of_detail",
      base::trace_event::MemoryDumpLevelOfDetailToString(
          request->args.level_of_detail));

  request->failed_memory_dump_count = 0;

  // Note: the service process itself is registered as a ClientProcess and
  // will be treated like any other process for the sake of memory dumps.
  request->pending_responses.clear();

  for (const auto& client_info : clients) {
    mojom::ClientProcess* client = client_info.client;

    // If we're only looking for a single pid process, then ignore clients
    // with different pid.
    if (request->args.pid != base::kNullProcessId &&
        request->args.pid != client_info.pid) {
      continue;
    }

    request->responses[client].process_id = client_info.pid;
    request->responses[client].process_type = client_info.process_type;
    request->responses[client].service_names = client_info.service_names;

    // Don't request a chrome memory dump at all if the client only wants the
    // a memory footprint.
    //
    // This must occur before the call to RequestOSMemoryDump, as
    // ClientProcessImpl will [for macOS], delay the calculations for the
    // OSMemoryDump until the Chrome memory dump is finished. See
    // https://bugs.chromium.org/p/chromium/issues/detail?id=812346#c16 for more
    // details.
    if (!request->args.memory_footprint_only) {
      request->pending_responses.insert({client, ResponseType::kChromeDump});
      client->RequestChromeMemoryDump(
          request->GetRequestArgs(),
          base::BindOnce(std::move(chrome_callback), client));
    }

// On most platforms each process can dump data about their own process
// so ask each process to do so Linux is special see below.
#if !defined(OS_LINUX)
    request->pending_responses.insert({client, ResponseType::kOSDump});
    client->RequestOSMemoryDump(request->memory_map_option(),
                                {base::kNullProcessId},
                                base::BindOnce(os_callback, client));
#endif  // !defined(OS_LINUX)

    // If we are in the single pid case, then we've already found the only
    // process we're looking for.
    if (request->args.pid != base::kNullProcessId)
      break;
  }

// In some cases, OS stats can only be dumped from a privileged process to
// get around to sandboxing/selinux restrictions (see crbug.com/461788).
#if defined(OS_LINUX)
  std::vector<base::ProcessId> pids;
  mojom::ClientProcess* browser_client = nullptr;
  pids.reserve(request->args.pid == base::kNullProcessId ? clients.size() : 1);
  for (const auto& client_info : clients) {
    if (request->args.pid == base::kNullProcessId ||
        client_info.pid == request->args.pid) {
      pids.push_back(client_info.pid);
    }
    if (client_info.process_type == mojom::ProcessType::BROWSER) {
      browser_client = client_info.client;
    }
  }
  if (clients.size() > 0) {
    DCHECK(browser_client);
  }
  if (browser_client && pids.size() > 0) {
    request->pending_responses.insert({browser_client, ResponseType::kOSDump});
    auto callback = base::BindOnce(os_callback, browser_client);
    browser_client->RequestOSMemoryDump(request->memory_map_option(), pids,
                                        std::move(callback));
  }
#endif  // defined(OS_LINUX)

  // In this case, we have not found the pid we are looking for so increment
  // the failed dump count and exit.
  if (request->args.pid != base::kNullProcessId &&
      request->pending_responses.empty()) {
    request->failed_memory_dump_count++;
    return;
  }
}

// static
void QueuedRequestDispatcher::SetUpAndDispatchVmRegionRequest(
    QueuedVmRegionRequest* request,
    const std::vector<ClientInfo>& clients,
    const std::vector<base::ProcessId>& desired_pids,
    const OsCallback& os_callback) {
// On Linux, OS stats can only be dumped from a privileged process to
// get around to sandboxing/selinux restrictions (see crbug.com/461788).
#if defined(OS_LINUX)
  mojom::ClientProcess* browser_client = nullptr;
  base::ProcessId browser_client_pid = 0;
  for (const auto& client_info : clients) {
    if (client_info.process_type == mojom::ProcessType::BROWSER) {
      browser_client = client_info.client;
      browser_client_pid = client_info.pid;
      break;
    }
  }

  if (!browser_client) {
    DLOG(ERROR) << "Missing browser client.";
    return;
  }

  request->pending_responses.insert(browser_client);
  request->responses[browser_client].process_id = browser_client_pid;
  auto callback = base::BindOnce(os_callback, browser_client);
  browser_client->RequestOSMemoryDump(mojom::MemoryMapOption::MODULES,
                                      desired_pids, std::move(callback));
#else
  for (const auto& client_info : clients) {
    if (std::find(desired_pids.begin(), desired_pids.end(), client_info.pid) !=
        desired_pids.end()) {
      mojom::ClientProcess* client = client_info.client;
      request->pending_responses.insert(client);
      request->responses[client].process_id = client_info.pid;
      client->RequestOSMemoryDump(mojom::MemoryMapOption::MODULES,
                                  {base::kNullProcessId},
                                  base::BindOnce(os_callback, client));
    }
  }
#endif  // defined(OS_LINUX)
}

// static
QueuedRequestDispatcher::VmRegions
QueuedRequestDispatcher::FinalizeVmRegionRequest(
    QueuedVmRegionRequest* request) {
  VmRegions results;
  for (auto& response : request->responses) {
    const base::ProcessId& original_pid = response.second.process_id;

    // |response| accumulates the replies received by each client process.
    // On Linux, the browser process will provide all OS dumps. On non-Linux,
    // each client process provides 1 OS dump, % the case where the client is
    // disconnected mid dump.
    OSMemDumpMap& extra_os_dumps = response.second.os_dumps;
#if defined(OS_LINUX)
    for (auto& kv : extra_os_dumps) {
      auto pid = kv.first == base::kNullProcessId ? original_pid : kv.first;
      DCHECK(results.find(pid) == results.end());
      results.emplace(pid, std::move(kv.second->memory_maps));
    }
#else
    // This can be empty if the client disconnects before providing both
    // dumps. See UnregisterClientProcess().
    DCHECK_LE(extra_os_dumps.size(), 1u);
    for (auto& kv : extra_os_dumps) {
      // When the OS dump comes from child processes, the pid is supposed to be
      // not used. We know the child process pid at the time of the request and
      // also wouldn't trust pids coming from child processes.
      DCHECK_EQ(base::kNullProcessId, kv.first);

      // Check we don't receive duplicate OS dumps for the same process.
      DCHECK(results.find(original_pid) == results.end());

      results.emplace(original_pid, std::move(kv.second->memory_maps));
    }
#endif
  }
  return results;
}

void QueuedRequestDispatcher::Finalize(QueuedRequest* request,
                                       TracingObserver* tracing_observer) {
  DCHECK(request->dump_in_progress);
  DCHECK(request->pending_responses.empty());

  // Reconstruct a map of pid -> ProcessMemoryDump by reassembling the responses
  // received by the clients for this dump. In some cases the response coming
  // from one client can also provide the dump of OS counters for other
  // processes. A concrete case is Linux, where the browser process provides
  // details for the child processes to get around sandbox restrictions on
  // opening /proc pseudo files.

  // All the pointers in the maps will continue to be owned by |request|
  // which outlives these containers.
  std::map<base::ProcessId, mojom::ProcessType> pid_to_process_type;
  std::map<base::ProcessId, std::vector<std::string>> pid_to_service_names;
  std::map<base::ProcessId, const base::trace_event::ProcessMemoryDump*>
      pid_to_pmd;
  std::map<base::ProcessId, mojom::RawOSMemDump*> pid_to_os_dump;
  for (auto& response : request->responses) {
    const base::ProcessId& original_pid = response.second.process_id;
    pid_to_process_type[original_pid] = response.second.process_type;

    std::vector<std::string>& service_names_for_pid =
        pid_to_service_names[original_pid];
    service_names_for_pid.insert(service_names_for_pid.begin(),
                                 response.second.service_names.begin(),
                                 response.second.service_names.end());

    // |chrome_dump| can be nullptr if this was a OS-counters only response.
    pid_to_pmd[original_pid] = response.second.chrome_dump.get();

    // |response| accumulates the replies received by each client process.
    // Depending on the OS each client process might return 1 chrome + 1 OS
    // dump each or, in the case of Linux, only 1 chrome dump each % the
    // browser process which will provide all the OS dumps.
    // In the former case (!OS_LINUX) we expect client processes to have
    // exactly one OS dump in their |response|, % the case when they
    // unexpectedly disconnect in the middle of a dump (e.g. because they
    // crash). In the latter case (OS_LINUX) we expect the full map to come
    // from the browser process response.
    OSMemDumpMap& extra_os_dumps = response.second.os_dumps;
#if defined(OS_LINUX)
    for (const auto& kv : extra_os_dumps) {
      auto pid = kv.first == base::kNullProcessId ? original_pid : kv.first;
      DCHECK_EQ(pid_to_os_dump[pid], nullptr);
      pid_to_os_dump[pid] = kv.second.get();
    }
#else
    // This can be empty if the client disconnects before providing both
    // dumps. See UnregisterClientProcess().
    DCHECK_LE(extra_os_dumps.size(), 1u);
    for (const auto& kv : extra_os_dumps) {
      // When the OS dump comes from child processes, the pid is supposed to be
      // not used. We know the child process pid at the time of the request and
      // also wouldn't trust pids coming from child processes.
      DCHECK_EQ(base::kNullProcessId, kv.first);

      // Check we don't receive duplicate OS dumps for the same process.
      DCHECK_EQ(pid_to_os_dump[original_pid], nullptr);

      pid_to_os_dump[original_pid] = kv.second.get();
    }
#endif
  }  // for (response : request->responses)

  // Generate the global memory graph from the map of pids to dumps, removing
  // weak nodes.
  std::unique_ptr<GlobalDumpGraph> global_graph =
      GraphProcessor::CreateMemoryGraph(pid_to_pmd);
  GraphProcessor::RemoveWeakNodesFromGraph(global_graph.get());

  // Compute the shared memory footprint for each process from the graph.
  std::map<base::ProcessId, uint64_t> shared_footprints =
      GraphProcessor::ComputeSharedFootprintFromGraph(*global_graph);

  // Perform the rest of the computation on the graph.
  GraphProcessor::AddOverheadsAndPropogateEntries(global_graph.get());
  GraphProcessor::CalculateSizesForGraph(global_graph.get());

  // Build up the global dump by iterating on the |valid| process dumps.
  mojom::GlobalMemoryDumpPtr global_dump(mojom::GlobalMemoryDump::New());
  global_dump->start_time = request->start_time;
  global_dump->process_dumps.reserve(request->responses.size());
  global_dump->aggregated_metrics = mojom::AggregatedMetrics::New();
  for (const auto& response : request->responses) {
    base::ProcessId pid = response.second.process_id;

    // On Linux, we may also have the browser process as a response.
    // Just ignore it if we are looking for the single pid case.
    if (request->args.pid != base::kNullProcessId && pid != request->args.pid)
      continue;

    // These pointers are owned by |request|.
    mojom::RawOSMemDump* raw_os_dump = pid_to_os_dump[pid];
    auto* raw_chrome_dump = pid_to_pmd[pid];

    // If we have the OS dump we should have the platform private footprint.
    DCHECK(!raw_os_dump || raw_os_dump->platform_private_footprint);

    // If the raw dump exists, create a summarised version of it.
    mojom::OSMemDumpPtr os_dump = nullptr;
    if (raw_os_dump) {
      uint64_t shared_resident_kb = 0;
#if defined(OS_MACOSX)
      // The resident, anonymous shared memory for each process is only relevant
      // on macOS.
      const auto process_graph_it =
          global_graph->process_dump_graphs().find(pid);
      if (process_graph_it != global_graph->process_dump_graphs().end()) {
        const auto& process_graph = process_graph_it->second;
        auto* node = process_graph->FindNode("shared_memory");
        if (node) {
          const auto& entry =
              node->entries()->find(MemoryAllocatorDump::kNameSize);
          if (entry != node->entries()->end())
            shared_resident_kb = entry->second.value_uint64 / 1024;
        }
      }
#endif
      os_dump = CreatePublicOSDump(
          *raw_os_dump, base::saturated_cast<uint32_t>(shared_resident_kb));
      os_dump->shared_footprint_kb =
          base::saturated_cast<uint32_t>(shared_footprints[pid] / 1024);
    }

    // Trace the OS and Chrome dumps if they exist.
    if (request->args.add_to_trace) {
      if (raw_os_dump) {
        bool trace_os_success = tracing_observer->AddOsDumpToTraceIfEnabled(
            request->GetRequestArgs(), pid, os_dump.get(),
            &raw_os_dump->memory_maps);
        if (!trace_os_success)
          request->failed_memory_dump_count++;
      }

      if (raw_chrome_dump) {
        bool trace_chrome_success = AddChromeMemoryDumpToTrace(
            request->GetRequestArgs(), pid, *raw_chrome_dump, *global_graph,
            pid_to_process_type, tracing_observer);
        if (!trace_chrome_success)
          request->failed_memory_dump_count++;
      }
    }

    bool valid = false;
    if (request->args.memory_footprint_only) {
      valid = raw_os_dump;
    } else {
      // Ignore incomplete results (can happen if the client
      // crashes/disconnects).
      valid = raw_os_dump && raw_chrome_dump &&
              (request->memory_map_option() == mojom::MemoryMapOption::NONE ||
               (raw_os_dump && !raw_os_dump->memory_maps.empty()));
    }

    if (!valid)
      continue;

    mojom::ProcessMemoryDumpPtr pmd = mojom::ProcessMemoryDump::New();
    pmd->pid = pid;
    pmd->process_type = pid_to_process_type[pid];
    pmd->service_names = pid_to_service_names[pid];
    pmd->os_dump = std::move(os_dump);

    // If we have to return a summary, add all entries for the requested
    // allocator dumps.
    if (request->should_return_summaries() &&
        !request->args.memory_footprint_only) {
      const auto& process_graph =
          global_graph->process_dump_graphs().find(pid)->second;
      for (const std::string& name : request->args.allocator_dump_names) {
        auto* node = process_graph->FindNode(name);
        // Silently ignore any missing node in the process graph.
        if (!node)
          continue;
        base::flat_map<std::string, uint64_t> numeric_entries;
        for (const auto& entry : *node->entries()) {
          if (entry.second.type == Node::Entry::Type::kUInt64)
            numeric_entries.emplace(entry.first, entry.second.value_uint64);
        }
        pmd->chrome_allocator_dumps.emplace(
            name, mojom::AllocatorMemDump::New(std::move(numeric_entries)));
      }
    }

    global_dump->process_dumps.push_back(std::move(pmd));
  }

#if BUILDFLAG(SUPPORTS_CODE_ORDERING)
  size_t native_resident_kb =
      ReportGlobalNativeCodeResidentMemoryKb(pid_to_os_dump);
  global_dump->aggregated_metrics->native_library_resident_kb =
      native_resident_kb;
#endif  // BUILDFLAG(SUPPORTS_CODE_ORDERING)

  const bool global_success = request->failed_memory_dump_count == 0;

  // In the single process-case, we want to ensure that global_success
  // is true if and only if global_dump is not nullptr.
  if (request->args.pid != base::kNullProcessId && !global_success) {
    global_dump = nullptr;
  }
  auto& callback = request->callback;
  std::move(callback).Run(global_success, request->dump_guid,
                          std::move(global_dump));
  UMA_HISTOGRAM_MEDIUM_TIMES("Memory.Experimental.Debug.GlobalDumpDuration",
                             base::TimeTicks::Now() - request->start_time);
  UMA_HISTOGRAM_COUNTS_1000(
      "Memory.Experimental.Debug.FailedProcessDumpsPerGlobalDump",
      request->failed_memory_dump_count);

  char guid_str[20];
  sprintf(guid_str, "0x%" PRIx64, request->dump_guid);
  TRACE_EVENT_NESTABLE_ASYNC_END2(
      base::trace_event::MemoryDumpManager::kTraceCategory, "GlobalMemoryDump",
      TRACE_ID_LOCAL(request->dump_guid), "dump_guid", TRACE_STR_COPY(guid_str),
      "success", global_success);
}

bool QueuedRequestDispatcher::AddChromeMemoryDumpToTrace(
    const base::trace_event::MemoryDumpRequestArgs& args,
    base::ProcessId pid,
    const base::trace_event::ProcessMemoryDump& raw_chrome_dump,
    const GlobalDumpGraph& global_graph,
    const std::map<base::ProcessId, mojom::ProcessType>& pid_to_process_type,
    TracingObserver* tracing_observer) {
  bool is_chrome_tracing_enabled =
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableChromeTracingComputation);
  if (!is_chrome_tracing_enabled) {
    return tracing_observer->AddChromeDumpToTraceIfEnabled(args, pid,
                                                           &raw_chrome_dump);
  }
  if (!tracing_observer->ShouldAddToTrace(args))
    return false;

  const GlobalDumpGraph::Process& process =
      *global_graph.process_dump_graphs().find(pid)->second;

  std::unique_ptr<TracedValue> traced_value;
  if (pid_to_process_type.find(pid)->second == mojom::ProcessType::BROWSER) {
    traced_value = GetChromeDumpAndGlobalAndEdgesTracedValue(
        process, *global_graph.shared_memory_graph(), global_graph.edges());
  } else {
    traced_value = GetChromeDumpTracedValue(process);
  }
  tracing_observer->AddToTrace(args, pid, std::move(traced_value));

  return true;
}

QueuedRequestDispatcher::ClientInfo::ClientInfo(
    mojom::ClientProcess* client,
    base::ProcessId pid,
    mojom::ProcessType process_type,
    std::vector<std::string> service_names)
    : client(client),
      pid(pid),
      process_type(process_type),
      service_names(std::move(service_names)) {}
QueuedRequestDispatcher::ClientInfo::ClientInfo(ClientInfo&& other) = default;
QueuedRequestDispatcher::ClientInfo::~ClientInfo() {}

}  // namespace memory_instrumentation
