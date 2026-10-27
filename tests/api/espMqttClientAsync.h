#pragma once
#include <functional>
namespace espMqttClientTypes {
enum class DisconnectReason { TCP_DISCONNECTED };
using OnMessageCallback = std::function<void()>;
}
