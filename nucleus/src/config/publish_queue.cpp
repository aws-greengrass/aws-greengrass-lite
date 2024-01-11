#include "publish_queue.hpp"
#include "channel/channel_callbacks.hpp"
#include "scope/context_full.hpp"

namespace config {

    void PublishQueue::publish(config::PublishAction action) {
        if(!_channel->isListening()) {
            start();
        }
        _channel->write(action);
    }

    void PublishQueue::start() {
        traits::PublishQueueStub stub{};
        _channel->setListenCallback(stub);
        _channel->setCloseCallback(stub);
    }

    void PublishQueue::stop() {
        _channel->close();
        _channel->drain();
    }

    void PublishQueue::drainQueue() {
        _channel->drain();
    }

    PublishQueue::PublishQueue(const scope::UsingContext &context)
        : scope::UsesContext(context),
          _channel(std::make_shared<channel::ChannelBase<traits::PublishQueueTraits>>(context)) {
    }
} // namespace config
