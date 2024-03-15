#include "api_error_trap.hpp"
#include "data/shared_list.hpp"
#include "pubsub/local_topics.hpp"
#include "scope/context_full.hpp"
#include "tasks/task.hpp"
#include "tasks/task_callbacks.hpp"
#include <cpp_api.hpp>

ggapiErrorKind ggapiIsSubscription(ggapiObjHandle handle, ggapiBool *pBool) noexcept {
    return apiImpl::catchErrorToKind([handle, pBool]() {
        auto ss{scope::context()->objFromInt(handle)};
        apiImpl::setBool(pBool, std::dynamic_pointer_cast<pubsub::Listener>(ss) != nullptr);
    });
}

ggapiErrorKind ggapiIsPromise(ggapiObjHandle handle, ggapiBool *pBool) noexcept {
    return apiImpl::catchErrorToKind([handle, pBool]() {
        auto ss{scope::context()->objFromInt(handle)};
        apiImpl::setBool(pBool, std::dynamic_pointer_cast<pubsub::Promise>(ss) != nullptr);
    });
}

ggapiErrorKind ggapiIsFuture(ggapiObjHandle handle, ggapiBool *pBool) noexcept {
    return apiImpl::catchErrorToKind([handle, pBool]() {
        auto ss{scope::context()->objFromInt(handle)};
        apiImpl::setBool(pBool, std::dynamic_pointer_cast<pubsub::FutureBase>(ss) != nullptr);
    });
}

ggapiErrorKind ggapiCreatePromise(ggapiObjHandle *pHandle) noexcept {
    return apiImpl::catchErrorToKind([pHandle]() {
        auto obj = scope::makeObject<pubsub::Promise>();
        *pHandle = scope::asIntHandle(obj);
    });
}

ggapiErrorKind ggapiSubscribeToTopic(
    ggapiSymbol topic,
    ggapiObjHandle callbackHandle,
    ggapiObjHandle *outListener) noexcept {

    return apiImpl::catchErrorToKind([topic, callbackHandle, outListener]() {
        auto context = scope::context();
        auto callback = context->objFromInt<tasks::Callback>(callbackHandle);
        *outListener = scope::asIntHandle(
            context->lpcTopics().subscribe(context->symbolFromInt(topic), callback));
    });
}

ggapiErrorKind ggapiCallTopicFirst(
    ggapiSymbol topic, ggapiObjHandle data, ggapiObjHandle *outFuture) noexcept {

    return apiImpl::catchErrorToKind([topic, data, outFuture]() {
        auto context = scope::context();
        auto topicSymbol = context->symbolFromInt(topic);
        auto dataObj = context->objFromInt<data::ContainerModelBase>(data);
        auto future = context->lpcTopics().callFirst(topicSymbol, dataObj);
        *outFuture = scope::asIntHandle(future);
    });
}

ggapiErrorKind ggapiCallTopicAll(
    ggapiSymbol topic, ggapiObjHandle data, ggapiObjHandle *outListOfFutures) noexcept {

    return apiImpl::catchErrorToKind([topic, data, outListOfFutures]() {
        auto context = scope::context();
        auto topicSymbol = context->symbolFromInt(topic);
        auto dataObj = context->objFromInt<data::ContainerModelBase>(data);
        auto futureVec = context->lpcTopics().callAll(topicSymbol, dataObj);
        auto listObj = scope::makeObject<data::SharedList>();
        for(const auto &i : futureVec) {
            listObj->insert(-1, i);
        }
        *outListOfFutures = scope::asIntHandle(listObj);
    });
}

ggapiErrorKind ggapiCallDirect(
    ggapiObjHandle target, ggapiObjHandle data, ggapiObjHandle *outFuture) noexcept {

    return apiImpl::catchErrorToKind([target, data, outFuture]() {
        auto context = scope::context();
        auto targetObj = context->objFromInt<pubsub::Listener>(target);
        auto dataObj = context->objFromInt<data::ContainerModelBase>(data);
        auto future = targetObj->call(dataObj);
        *outFuture = scope::asIntHandle(future);
    });
}

ggapiErrorKind ggapiPromiseSetValue(
    ggapiObjHandle promiseHandle, ggapiObjHandle newValue) noexcept {

    return apiImpl::catchErrorToKind([promiseHandle, newValue]() {
        auto context = scope::context();
        auto promiseObj = context->objFromInt<pubsub::Promise>(promiseHandle);
        auto newValueObj = context->objFromInt<data::ContainerModelBase>(newValue);
        promiseObj->setValue(newValueObj);
    });
}

ggapiErrorKind ggapiPromiseSetError(
    ggapiObjHandle promiseHandle,
    ggapiSymbol errorKind,
    const char *str,
    uint32_t strlen) noexcept {

    return apiImpl::catchErrorToKind([promiseHandle, errorKind, str, strlen]() {
        auto context = scope::context();
        auto kindSymbol = context->symbolFromInt(errorKind);
        std::string msg(str, strlen);
        auto promiseObj = context->objFromInt<pubsub::Promise>(promiseHandle);
        promiseObj->setError(errors::Error(kindSymbol, msg));
    });
}

ggapiErrorKind ggapiPromiseCancel(ggapiObjHandle promiseHandle) noexcept {

    return apiImpl::catchErrorToKind([promiseHandle]() {
        auto context = scope::context();
        auto promiseObj = context->objFromInt<pubsub::Promise>(promiseHandle);
        promiseObj->cancel();
    });
}

ggapiErrorKind ggapiFutureFromPromise(ggapiObjHandle promise, ggapiObjHandle *outFuture) noexcept {

    return apiImpl::catchErrorToKind([promise, outFuture]() {
        auto context = scope::context();
        auto promiseObj = context->objFromInt<pubsub::Promise>(promise);
        auto future = promiseObj->getFuture();
        *outFuture = scope::asIntHandle(future);
    });
}

ggapiErrorKind ggapiFutureGetValue(ggapiObjHandle futureHandle, ggapiObjHandle *outValue) noexcept {

    return apiImpl::catchErrorToKind([futureHandle, outValue]() {
        auto context = scope::context();
        auto futureObj = context->objFromInt<pubsub::FutureBase>(futureHandle);
        auto value = futureObj->getValue();
        *outValue = scope::asIntHandle(value);
    });
}

ggapiErrorKind ggapiFutureIsValid(ggapiObjHandle futureHandle, ggapiBool *pBool) noexcept {

    return apiImpl::catchErrorToKind([futureHandle, pBool]() {
        auto context = scope::context();
        auto futureObj = context->objFromInt<pubsub::FutureBase>(futureHandle);
        apiImpl::setBool(pBool, futureObj->isValid());
    });
}

ggapiErrorKind ggapiFutureWait(
    ggapiObjHandle futureHandle, int32_t timeout, ggapiBool *pBool) noexcept {

    return apiImpl::catchErrorToKind([futureHandle, timeout, pBool]() {
        auto context = scope::context();
        auto futureObj = context->objFromInt<pubsub::FutureBase>(futureHandle);
        tasks::ExpireTime expTime = tasks::ExpireTime::fromNowMillis(timeout);
        apiImpl::setBool(pBool, futureObj->waitUntil(expTime));
    });
}

ggapiErrorKind ggapiFutureAddCallback(
    ggapiObjHandle futureHandle, ggapiObjHandle callback) noexcept {

    return apiImpl::catchErrorToKind([futureHandle, callback]() {
        auto context = scope::context();
        auto futureObj = context->objFromInt<pubsub::FutureBase>(futureHandle);
        auto callbackObj = context->objFromInt<tasks::Callback>(callback);
        futureObj->addCallback(callbackObj);
    });
}
