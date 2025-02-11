// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/modules/csspaint/paint_worklet_proxy_client.h"

#include "base/single_thread_task_runner.h"
#include "third_party/blink/renderer/core/css/cssom/paint_worklet_input.h"
#include "third_party/blink/renderer/core/dom/document.h"
#include "third_party/blink/renderer/core/frame/web_frame_widget_base.h"
#include "third_party/blink/renderer/core/frame/web_local_frame_impl.h"
#include "third_party/blink/renderer/core/workers/worker_thread.h"
#include "third_party/blink/renderer/modules/csspaint/css_paint_definition.h"
#include "third_party/blink/renderer/modules/csspaint/main_thread_document_paint_definition.h"
#include "third_party/blink/renderer/modules/csspaint/paint_worklet.h"
#include "third_party/blink/renderer/platform/cross_thread_functional.h"
#include "third_party/blink/renderer/platform/graphics/image.h"
#include "third_party/blink/renderer/platform/graphics/paint_worklet_paint_dispatcher.h"

namespace blink {

const char PaintWorkletProxyClient::kSupplementName[] =
    "PaintWorkletProxyClient";

// static
PaintWorkletProxyClient* PaintWorkletProxyClient::Create(Document* document,
                                                         int worklet_id) {
  WebLocalFrameImpl* local_frame =
      WebLocalFrameImpl::FromFrame(document->GetFrame());
  PaintWorklet* paint_worklet = PaintWorklet::From(*document->domWindow());
  scoped_refptr<PaintWorkletPaintDispatcher> compositor_painter_dispatcher =
      local_frame->LocalRootFrameWidget()->EnsureCompositorPaintDispatcher();
  return MakeGarbageCollected<PaintWorkletProxyClient>(
      worklet_id, paint_worklet, std::move(compositor_painter_dispatcher));
}

PaintWorkletProxyClient::PaintWorkletProxyClient(
    int worklet_id,
    PaintWorklet* paint_worklet,
    scoped_refptr<PaintWorkletPaintDispatcher> compositor_paintee)
    : compositor_paintee_(std::move(compositor_paintee)),
      worklet_id_(worklet_id),
      state_(RunState::kUninitialized),
      main_thread_runner_(Thread::MainThread()->GetTaskRunner()),
      paint_worklet_(paint_worklet) {
  DCHECK(IsMainThread());
}

void PaintWorkletProxyClient::Trace(blink::Visitor* visitor) {
  visitor->Trace(document_definition_map_);
  Supplement<WorkerClients>::Trace(visitor);
  PaintWorkletPainter::Trace(visitor);
}

void PaintWorkletProxyClient::AddGlobalScope(WorkletGlobalScope* global_scope) {
  DCHECK(global_scope);
  DCHECK(global_scope->IsContextThread());
  if (state_ == RunState::kDisposed)
    return;
  DCHECK(state_ == RunState::kUninitialized);

  global_scopes_.push_back(To<PaintWorkletGlobalScope>(global_scope));

  // Wait for all global scopes to be set before registering.
  if (global_scopes_.size() < PaintWorklet::kNumGlobalScopes) {
    return;
  }

  // All the global scopes that share a single PaintWorkletProxyClient are
  // running on the same thread, and so we can just grab the task runner from
  // the last one to call this function and use that.
  scoped_refptr<base::SingleThreadTaskRunner> global_scope_runner =
      global_scope->GetThread()->GetTaskRunner(TaskType::kMiscPlatformAPI);
  state_ = RunState::kWorking;

  compositor_paintee_->RegisterPaintWorkletPainter(this, global_scope_runner);
}

void PaintWorkletProxyClient::RegisterCSSPaintDefinition(
    const String& name,
    CSSPaintDefinition* definition,
    ExceptionState& exception_state) {
  DocumentPaintDefinition* document_definition;
  if (document_definition_map_.Contains(name)) {
    document_definition = document_definition_map_.at(name);
    if (document_definition == kInvalidDocumentPaintDefinition)
      return;
    if (!document_definition->RegisterAdditionalPaintDefinition(*definition)) {
      document_definition_map_.Set(name, kInvalidDocumentPaintDefinition);
      exception_state.ThrowDOMException(
          DOMExceptionCode::kNotSupportedError,
          "A class with name:'" + name +
              "' was registered with a different definition.");
      return;
    }
  } else {
    document_definition =
        MakeGarbageCollected<DocumentPaintDefinition>(definition);
    document_definition_map_.Set(name, document_definition);
  }

  // Notify the main thread only once all global scopes have registered the same
  // named paint definition (with the same definition as well).
  if (document_definition->GetRegisteredDefinitionCount() ==
      PaintWorklet::kNumGlobalScopes) {
    auto main_thread_definition =
        std::make_unique<MainThreadDocumentPaintDefinition>(definition);
    PostCrossThreadTask(
        *main_thread_runner_, FROM_HERE,
        CrossThreadBind(
            &PaintWorklet::RegisterMainThreadDocumentPaintDefinition,
            paint_worklet_, name,
            WTF::Passed(std::move(main_thread_definition))));
  }
}

void PaintWorkletProxyClient::Dispose() {
  if (state_ == RunState::kWorking) {
    compositor_paintee_->UnregisterPaintWorkletPainter(this);
  }
  compositor_paintee_ = nullptr;

  state_ = RunState::kDisposed;

  // At worklet scope termination break the reference cycle between
  // PaintWorkletGlobalScope and PaintWorkletProxyClient.
  global_scopes_.clear();
}

sk_sp<PaintRecord> PaintWorkletProxyClient::Paint(
    CompositorPaintWorkletInput* compositor_input) {
  // TODO: Can this happen? We don't register till all are here.
  if (global_scopes_.IsEmpty())
    return sk_make_sp<PaintRecord>();

  // PaintWorklets are stateless by spec. There are two ways script might try to
  // inject state:
  //   * From one PaintWorklet to another, in the same frame.
  //   * Inside the same PaintWorklet, across frames.
  //
  // To discourage both of these, we randomize selection of the global scope.
  // TODO(smcgruer): Once we are passing bundles of PaintWorklets here, we
  // should shuffle the bundle randomly and then assign half to the first global
  // scope, and half to the rest.
  DCHECK_EQ(global_scopes_.size(), PaintWorklet::kNumGlobalScopes);
  PaintWorkletGlobalScope* global_scope =
      global_scopes_[base::RandInt(0, PaintWorklet::kNumGlobalScopes - 1)];

  PaintWorkletInput* input = static_cast<PaintWorkletInput*>(compositor_input);
  CSSPaintDefinition* definition =
      global_scope->FindDefinition(input->NameCopy());

  return definition->Paint(FloatSize(input->GetSize()), input->EffectiveZoom(),
                           nullptr, nullptr);
}

// static
PaintWorkletProxyClient* PaintWorkletProxyClient::From(WorkerClients* clients) {
  return Supplement<WorkerClients>::From<PaintWorkletProxyClient>(clients);
}

void ProvidePaintWorkletProxyClientTo(WorkerClients* clients,
                                      PaintWorkletProxyClient* client) {
  clients->ProvideSupplement(client);
}

}  // namespace blink
