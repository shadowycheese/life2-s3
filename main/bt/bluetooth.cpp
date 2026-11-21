#include "bt/bluetooth.h"

Bluetooth *Bluetooth::instance = nullptr;

int gatt_callback_wrapper(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return Bluetooth::instance->gatt_callback(conn_handle, attr_handle, ctxt, arg);
}

void nimble_host_task_wrapper(void *)
{
    Bluetooth::instance->nimble_host_task();
}

void ble_app_on_sync_wrapper(void)
{
    Bluetooth::instance->ble_app_on_sync();
}

int ble_gap_event_handler_wrapper(struct ble_gap_event *event, void *arg)
{
    return Bluetooth::instance->handle_gap_event(event);
}

void register_bluetooth_event_handler(uint8_t command, bluetooth_event_handler_t handler)
{
    Bluetooth::instance->_handlers[command - COMMAND_START] = handler;
}
