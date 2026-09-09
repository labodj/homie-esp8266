// Private MQTT helpers shared with native regression tests; no Arduino I/O.
void BootNormal::_onMqttPublish(uint16_t id) {
  _mqttOfflineAck.acknowledge(id);
  if (!_enqueuePendingMqttAck(id)) {
    incrementDropCounters(_pendingMqttAcksDropped, _pendingMqttAcksDroppedTotal);
  }
}

#if HOMIE_PENDING_MQTT_MESSAGE_PREALLOCATED
bool BootNormal::__fillPreallocatedPayloadBuffer(const uint8_t* payload, size_t len, size_t index, size_t total) {
  if (total > PENDING_MQTT_MESSAGE_MAX_PAYLOAD_LENGTH || index > total || len > total - index) {
    incrementDropCounters(_pendingMqttMessagesDropped, _pendingMqttMessagesDroppedTotal);
    return true;
  }

  memcpy(_mqttPreallocatedPayloadBuffer.data() + index, payload, len);

  if (index + len != total)
    return true;

  _mqttPreallocatedPayloadBuffer[total] = '\0';
  return false;
}
#endif

bool HomieInternals::BootNormal::__fillPayloadBuffer(std::unique_ptr<char[]>& payloadBuffer, size_t& payloadBufferCapacity, const uint8_t* payload, size_t len, size_t index, size_t total) {
  // OTA is handled before this function and streams independently of this cap.
  if (total > PENDING_MQTT_MESSAGE_MAX_PAYLOAD_LENGTH || index > total || len > total - index) {
    incrementDropCounters(_pendingMqttMessagesDropped, _pendingMqttMessagesDroppedTotal);
    return true;
  }

  if (index == 0 && payloadBufferCapacity < total + 1) {
    std::unique_ptr<char[]> newPayloadBuffer(new (std::nothrow) char[total + 1]);
    if (!newPayloadBuffer) {
      payloadBuffer.reset();
      payloadBufferCapacity = 0;
      incrementDropCounters(_pendingMqttMessagesDropped, _pendingMqttMessagesDroppedTotal);
      return true;
    }

    payloadBuffer = std::move(newPayloadBuffer);
    payloadBufferCapacity = total + 1;
  } else if (payloadBuffer == nullptr) {
    incrementDropCounters(_pendingMqttMessagesDropped, _pendingMqttMessagesDroppedTotal);
    return true;
  } else if (payloadBufferCapacity < total + 1) {
    incrementDropCounters(_pendingMqttMessagesDropped, _pendingMqttMessagesDroppedTotal);
    return true;
  }

  // copy payload into buffer
  memcpy(payloadBuffer.get() + index, payload, len);

  // return if payload buffer is not complete
  if (index + len != total)
    return true;
  // terminate buffer
  payloadBuffer.get()[total] = '\0';
  return false;
}
