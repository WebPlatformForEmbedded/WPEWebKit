/*
 * Copyright (C) 2013 Nokia Corporation and/or its subsidiary(-ies).
 * Copyright (C) 2015 Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#if ENABLE(MEDIA_STREAM)
#include "JSRTCPeerConnection.h"

#include "Dictionary.h"
#include "ExceptionCode.h"
#include "JSDOMBinding.h"
#include "JSDOMError.h"
#include "JSDOMPromise.h"
#include "JSRTCIceCandidate.h"
#include "JSRTCPeerConnectionErrorCallback.h"
#include "JSRTCSessionDescription.h"
#include "JSRTCSessionDescriptionCallback.h"
#include "JSVoidCallback.h"

using namespace JSC;

namespace WebCore {

EncodedJSValue JSC_HOST_CALL constructJSRTCPeerConnection(ExecState* exec)
{
    // Spec says that we must have at least one arument, the RTCConfiguration.
    if (exec->argumentCount() < 1)
        return throwVMError(exec, createNotEnoughArgumentsError(exec));

    ExceptionCode ec = 0;
    Dictionary rtcConfiguration(exec, exec->argument(0));
    if (exec->hadException())
        return JSValue::encode(jsUndefined());

    if (!rtcConfiguration.isObject())
        return throwVMError(exec, createTypeError(exec, "RTCPeerConnection argument must be a valid Dictionary"));

    DOMConstructorObject* jsConstructor = jsCast<DOMConstructorObject*>(exec->callee());
    ScriptExecutionContext* scriptExecutionContext = jsConstructor->scriptExecutionContext();
    if (!scriptExecutionContext)
        return throwVMError(exec, createReferenceError(exec, "RTCPeerConnection constructor associated document is unavailable"));

    RefPtr<RTCPeerConnection> peerConnection = RTCPeerConnection::create(*scriptExecutionContext, rtcConfiguration, ec);
    if (ec == TYPE_MISMATCH_ERR) {
        setDOMException(exec, ec);
        return throwVMError(exec, createTypeError(exec, "Invalid RTCPeerConnection constructor arguments"));
    }

    if (ec) {
        setDOMException(exec, ec);
        return throwVMError(exec, createTypeError(exec, "Error creating RTCPeerConnection"));
    }

    return JSValue::encode(CREATE_DOM_WRAPPER(jsConstructor->globalObject(), RTCPeerConnection, peerConnection.get()));
}

static JSValue createOfferOrAnswer(RTCPeerConnection& impl, void (RTCPeerConnection::*implFunction)(const Dictionary&, RTCPeerConnection::OfferAnswerResolveCallback, RTCPeerConnection::RejectCallback), JSDOMGlobalObject* globalObject, ExecState* exec)
{
    Dictionary options;

    if (exec->argumentCount() > 1 && exec->argument(0).isFunction() && exec->argument(1).isFunction()) {
        // legacy callbacks mode
        if (exec->argumentCount() > 2) {
            options = Dictionary(exec, exec->argument(2));
            if (!options.isObject()) {
                throwVMError(exec, createTypeError(exec, "Third argument must be a valid Dictionary"));
                return jsUndefined();
            }
        }

        RefPtr<RTCSessionDescriptionCallback> sessionDescriptionCallback = JSRTCSessionDescriptionCallback::create(asObject(exec->argument(0)), globalObject);
        RefPtr<RTCPeerConnectionErrorCallback> errorCallback = JSRTCPeerConnectionErrorCallback::create(asObject(exec->argument(1)), globalObject);

        RefPtr<RTCPeerConnection> protectedImpl = &impl;
        auto resolveCallback = [protectedImpl, sessionDescriptionCallback](RTCSessionDescription& description) mutable {
            RefPtr<RTCSessionDescription> protectedDescription = &description;
            protectedImpl->scriptExecutionContext()->postTask([sessionDescriptionCallback, protectedDescription](ScriptExecutionContext&) mutable {
                sessionDescriptionCallback->handleEvent(protectedDescription.get());
            });
        };
        auto rejectCallback = [protectedImpl, errorCallback](DOMError& error) mutable {
            RefPtr<DOMError> protectedError = &error;
            protectedImpl->scriptExecutionContext()->postTask([errorCallback, protectedError](ScriptExecutionContext&) mutable {
                errorCallback->handleEvent(protectedError.get());
            });
        };

        (impl.*implFunction)(options, WTF::move(resolveCallback), WTF::move(rejectCallback));

        return jsUndefined();
    }

    // Promised-based mode
    if (exec->argumentCount()) {
        options = Dictionary(exec, exec->argument(0));
        if (!options.isObject()) {
            throwVMError(exec, createTypeError(exec, "First argument must be a valid Dictionary"));
            return jsUndefined();
        }
    }

    JSPromiseDeferred* promiseDeferred = JSPromiseDeferred::create(exec, globalObject);
    DeferredWrapper wrapper(exec, globalObject, promiseDeferred);

    auto resolveCallback = [wrapper](RTCSessionDescription &description) mutable {
        wrapper.resolve(&description);
    };
    auto rejectCallback = [wrapper](DOMError& error) mutable {
        wrapper.reject(&error);
    };

    (impl.*implFunction)(options, WTF::move(resolveCallback), WTF::move(rejectCallback));

    return promiseDeferred->promise();
}

JSValue JSRTCPeerConnection::createOffer(ExecState* exec)
{
    return createOfferOrAnswer(impl(), &RTCPeerConnection::createOffer, globalObject(), exec);
}

JSValue JSRTCPeerConnection::createAnswer(ExecState* exec)
{
    return createOfferOrAnswer(impl(), &RTCPeerConnection::createAnswer, globalObject(), exec);
}

static JSValue setLocalOrRemoteDescription(RTCPeerConnection& impl, void (RTCPeerConnection::*implFunction)(RTCSessionDescription*, RTCPeerConnection::VoidResolveCallback, RTCPeerConnection::RejectCallback), JSDOMGlobalObject* globalObject, ExecState* exec)
{
    RefPtr<RTCSessionDescription> description = JSRTCSessionDescription::toWrapped(exec->argument(0));
    if (!description) {
        throwVMError(exec, createTypeError(exec, "First argument must be a RTCSessionDescription"));
        return jsUndefined();
    }

    if (exec->argumentCount() > 2 && exec->argument(1).isFunction() && exec->argument(2).isFunction()) {
        // legacy callbacks mode
        RefPtr<VoidCallback> voidCallback = JSVoidCallback::create(asObject(exec->argument(1)), globalObject);
        RefPtr<RTCPeerConnectionErrorCallback> errorCallback = JSRTCPeerConnectionErrorCallback::create(asObject(exec->argument(2)), globalObject);

        RefPtr<RTCPeerConnection> protectedImpl = &impl;
        auto resolveCallback = [protectedImpl, voidCallback]() mutable {
            protectedImpl->scriptExecutionContext()->postTask([voidCallback](ScriptExecutionContext&) mutable {
                voidCallback->handleEvent();
            });
        };
        auto rejectCallback = [protectedImpl, errorCallback](DOMError& error) mutable {
            RefPtr<DOMError> protectedError = &error;
            protectedImpl->scriptExecutionContext()->postTask([errorCallback, protectedError](ScriptExecutionContext&) mutable {
                errorCallback->handleEvent(protectedError.get());
            });
        };

        (impl.*implFunction)(description.get() , WTF::move(resolveCallback), WTF::move(rejectCallback));

        return jsUndefined();
    }

    // Promised-based mode
    JSPromiseDeferred* promiseDeferred = JSPromiseDeferred::create(exec, globalObject);
    DeferredWrapper wrapper(exec, globalObject, promiseDeferred);

    auto resolveCallback = [wrapper]() mutable {
        wrapper.resolve(false);
    };
    auto rejectCallback = [wrapper](DOMError& error) mutable {
        wrapper.reject(&error);
    };

    (impl.*implFunction)(description.get(), WTF::move(resolveCallback), WTF::move(rejectCallback));

    return promiseDeferred->promise();
}

JSValue JSRTCPeerConnection::setLocalDescription(ExecState* exec)
{
    return setLocalOrRemoteDescription(impl(), &RTCPeerConnection::setLocalDescription, globalObject(), exec);
}

JSValue JSRTCPeerConnection::setRemoteDescription(ExecState* exec)
{
    return setLocalOrRemoteDescription(impl(), &RTCPeerConnection::setRemoteDescription, globalObject(), exec);
}

JSValue JSRTCPeerConnection::addIceCandidate(ExecState* exec)
{
    RefPtr<RTCIceCandidate> candidate = JSRTCIceCandidate::toWrapped(exec->argument(0));
    if (!candidate) {
        throwVMError(exec, createTypeError(exec, "First argument must be a RTCIceCandidate"));
        return jsUndefined();
    }

    if (exec->argumentCount() > 2 && exec->argument(1).isFunction() && exec->argument(2).isFunction()) {
        // legacy callbacks mode
        RefPtr<VoidCallback> voidCallback = JSVoidCallback::create(asObject(exec->argument(1)), globalObject());
        RefPtr<RTCPeerConnectionErrorCallback> errorCallback = JSRTCPeerConnectionErrorCallback::create(asObject(exec->argument(2)), globalObject());

        RefPtr<RTCPeerConnection> protectedImpl = &impl();
        auto resolveCallback = [protectedImpl, voidCallback]() mutable {
            protectedImpl->scriptExecutionContext()->postTask([voidCallback](ScriptExecutionContext&) mutable {
                voidCallback->handleEvent();
            });
        };
        auto rejectCallback = [protectedImpl, errorCallback](DOMError& error) mutable {
            RefPtr<DOMError> protectedError = &error;
            protectedImpl->scriptExecutionContext()->postTask([errorCallback, protectedError](ScriptExecutionContext&) mutable {
                errorCallback->handleEvent(protectedError.get());
            });
        };

        impl().addIceCandidate(candidate.get() , WTF::move(resolveCallback), WTF::move(rejectCallback));

        return jsUndefined();
    }

    // Promised-based mode
    JSPromiseDeferred* promiseDeferred = JSPromiseDeferred::create(exec, globalObject());
    DeferredWrapper wrapper(exec, globalObject(), promiseDeferred);

    auto resolveCallback = [wrapper]() mutable {
        wrapper.resolve(false);
    };
    auto rejectCallback = [wrapper](DOMError& error) mutable {
        wrapper.reject(&error);
    };

    impl().addIceCandidate(candidate.get(), WTF::move(resolveCallback), WTF::move(rejectCallback));

    return promiseDeferred->promise();
}

} // namespace WebCore

#endif // ENABLE(MEDIA_STREAM)
