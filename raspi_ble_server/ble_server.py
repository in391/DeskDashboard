"""Run the Raspberry Pi BLE server lifecycle.

Process overview:
1. Initialize the GLib-backed D-Bus main loop and connect to the system bus.
2. Discover a Bluetooth adapter that supports both GATT and LE advertising.
3. Preload/cache payload data (sensor, subway, weather+calendar, news).
4. Build the BLE GATT hierarchy (application, service, notify/write chars).
5. Build and register the LE advertisement.
6. Register signal handlers and file monitoring for sensor updates.
7. Enter the GLib event loop to serve subscriptions/notifications.
8. On shutdown, unregister advertisement and application from BlueZ.
"""

import sys
import signal
import os
import dbus
import dbus.exceptions
import dbus.mainloop.glib
import dbus.service
from gi.repository import GLib, Gio
import json
from datetime import datetime
import time
import traceback

# Constants
BLUEZ_SERVICE_NAME = 'org.bluez'
GATT_MANAGER_IFACE = 'org.bluez.GattManager1'
DBUS_OM_IFACE = 'org.freedesktop.DBus.ObjectManager'
LE_ADVERTISING_MANAGER_IFACE = 'org.bluez.LEAdvertisingManager1'
GATT_SERVICE_IFACE = 'org.bluez.GattService1'
GATT_CHRC_IFACE = 'org.bluez.GattCharacteristic1'
LE_ADVERTISEMENT_IFACE = 'org.bluez.LEAdvertisement1'
DBUS_PROP_IFACE = 'org.freedesktop.DBus.Properties'

cache_payload_sensor = {} # Cache the latest sensor payload to push immediately on new subscriptions without waiting for file change.
cache_payload_weather_calendar = {} # Cache the latest weather and calendar payload to push immediately on new subscriptions without waiting for scheduled trigger.
cache_payload_subway = {} # Cache the latest subway status to push immediately on new subscriptions during commute hours.
cache_payload_news = {} # Cache the latest news headlines to push immediately on new subscriptions during morning hours.

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
SERVER_CONFIG_PATH = os.getenv(
    'BLE_SERVER_CONFIG_FILE',
    os.path.join(BASE_DIR, 'server_config.local.json')
)

def load_home_temp_json_path():
    config = {}
    if os.path.isfile(SERVER_CONFIG_PATH):
        try:
            with open(SERVER_CONFIG_PATH, 'r', encoding='utf-8') as f:
                config = json.load(f)
        except (json.JSONDecodeError, OSError) as error:
            raise RuntimeError(
                f'Invalid BLE server config file: {SERVER_CONFIG_PATH} ({error})'
            ) from error

    path = config.get('home_temp_json_path')
    service_uuid = config.get('service_uuid')
    characteristic_uuid = config.get('characteristic_uuid')
    request_characteristic_uuid = config.get('request_characteristic_uuid')

    return (
        os.path.expanduser(path),
        service_uuid.lower(),
        characteristic_uuid.lower(),
        request_characteristic_uuid.lower(),
    )


# Runtime configuration (local config/env; not hardcoded in repository)
(
    HOME_TEMP_JSON_PATH,
    SERVICE_UUID,
    CHARACTERISTIC_UUID,
    REQUEST_CHARACTERISTIC_UUID,
) = load_home_temp_json_path()

# Keep state so scheduled trigger fires once per minute at most.
last_triggered_minute = None
# for Calendar & Weather combined trigger
last_triggered_timestamp = None

class InvalidArgsException(dbus.exceptions.DBusException):
    _dbus_error_name = 'org.freedesktop.DBus.Error.InvalidArgs'

class BLEApplication(dbus.service.Object):
    def __init__(self, bus):
        self.path = '/'
        self.services = []
        dbus.service.Object.__init__(self, bus, self.path)

    def get_path(self):
        return dbus.ObjectPath(self.path)

    def add_service(self, service):
        self.services.append(service)

    @dbus.service.method(DBUS_OM_IFACE, out_signature='a{oa{sa{sv}}}')
    def GetManagedObjects(self):
        response = {}
        for service in self.services:
            response[service.get_path()] = service.get_properties()
            chrcs = service.get_characteristics()
            for chrc in chrcs:
                response[chrc.get_path()] = chrc.get_properties()
        return response

class BLEService(dbus.service.Object):
    def __init__(self, bus, index, uuid, primary):
        self.path = f'/org/bluez/example/service{index}'
        self.bus = bus
        self.uuid = uuid
        self.primary = primary
        self.characteristics = []
        dbus.service.Object.__init__(self, bus, self.path)

    def get_properties(self):
        return {
            GATT_SERVICE_IFACE: {
                'UUID': self.uuid,
                'Primary': self.primary,
                'Characteristics': dbus.Array([c.get_path() for c in self.characteristics], signature='o')
            }
        }

    def get_path(self):
        return dbus.ObjectPath(self.path)

    def add_characteristic(self, characteristic):
        self.characteristics.append(characteristic)

    def get_characteristics(self):
        return self.characteristics

    @dbus.service.method(DBUS_PROP_IFACE, in_signature='s', out_signature='a{sv}')
    def GetAll(self, interface):
        if interface != GATT_SERVICE_IFACE:
            raise InvalidArgsException()
        return self.get_properties()[GATT_SERVICE_IFACE]

class SensorCharacteristic(dbus.service.Object):
    def __init__(self, bus, index, service):
        self.path = f'{service.path}/char{index}'
        self.bus = bus
        self.uuid = CHARACTERISTIC_UUID
        self.service = service
        self.flags = ['read', 'notify']
        self.notifying = False
        self.tx_queue = []
        self.tx_timer_id = 0
        self.tx_interval_ms = int(os.getenv('BLE_NOTIFY_INTERVAL_MS', '120'))
        dbus.service.Object.__init__(self, bus, self.path)

    def get_properties(self):
        return {
            GATT_CHRC_IFACE: {
                'Service': self.service.get_path(),
                'UUID': self.uuid,
                'Flags': self.flags,
            }
        }

    def get_path(self):
        return dbus.ObjectPath(self.path)

    @dbus.service.method(DBUS_PROP_IFACE, in_signature='s', out_signature='a{sv}')
    def GetAll(self, interface):
        if interface != GATT_CHRC_IFACE:
            raise InvalidArgsException()
        return self.get_properties()[GATT_CHRC_IFACE]

    @dbus.service.signal(DBUS_PROP_IFACE, signature='sa{sv}as')
    def PropertiesChanged(self, interface, changed, invalidated):
        pass

    @dbus.service.method(GATT_CHRC_IFACE, in_signature='', out_signature='')
    def StartNotify(self):
        if not self.notifying:
            self.notifying = True
            print("ESP32 subscribed to notifications!")
            try:
                on_initial_connection()  # Push initial data immediately upon subscription
            except Exception as e:
                print(f"StartNotify initial push failed: {e}")
                traceback.print_exc()

    @dbus.service.method(GATT_CHRC_IFACE, in_signature='', out_signature='')
    def StopNotify(self):
        if self.notifying:
            self.notifying = False
            self.tx_queue.clear()
            if self.tx_timer_id:
                GLib.source_remove(self.tx_timer_id)
                self.tx_timer_id = 0
            print("ESP32 unsubscribed from notifications!")

    def push_data(self, data_dict):
        # Queue notifications to avoid burst loss on some BLE stacks.
        if not self.notifying:
            print("Skipped Push: No devices currently subscribed.")
            return

        self.tx_queue.append(data_dict)
        if not self.tx_timer_id:
            self.tx_timer_id = GLib.timeout_add(self.tx_interval_ms, self._drain_tx_queue)

    def _drain_tx_queue(self):
        if not self.notifying:
            self.tx_queue.clear()
            self.tx_timer_id = 0
            return False

        if not self.tx_queue:
            self.tx_timer_id = 0
            return False

        next_payload = self.tx_queue.pop(0)
        self._notify_now(next_payload)
        return True

    def _notify_now(self, data_dict):
        if not self.notifying:
            print("Skipped Push: No devices currently subscribed.")
            return

        json_str = json.dumps(data_dict)
        payload = json_str.encode('utf-8')
        value = [dbus.Byte(b) for b in payload]
            
        print(f"Triggered Push! Sending: {json_str}")
        self.PropertiesChanged(GATT_CHRC_IFACE, {'Value': dbus.Array(value, signature='y')}, [])

    @dbus.service.method(GATT_CHRC_IFACE, in_signature='a{sv}', out_signature='ay')
    def ReadValue(self, options):
        # Format dummy sensor data as a JSON string
        data_dict = {"temperature": 24.5, "humidity": 45.2, "status": "online"}
        json_str = json.dumps(data_dict)
        
        # Convert string to array of bytes
        payload = json_str.encode('utf-8')
        value = [dbus.Byte(b) for b in payload]
            
        print(f"ESP32 Read Request! Returning: {json_str}")
        return value

class RequestCharacteristic(dbus.service.Object):
    """Writable characteristic (...ba5) that accepts {"cmd":"get"} to trigger
    a full data push on the notify characteristic (...ba4)."""
    def __init__(self, bus, index, service):
        self.path = f'{service.path}/char{index}'
        self.bus = bus
        self.uuid = REQUEST_CHARACTERISTIC_UUID
        self.service = service
        self.flags = ['write', 'write-without-response']
        dbus.service.Object.__init__(self, bus, self.path)

    def get_properties(self):
        return {
            GATT_CHRC_IFACE: {
                'Service': self.service.get_path(),
                'UUID': self.uuid,
                'Flags': self.flags,
            }
        }

    def get_path(self):
        return dbus.ObjectPath(self.path)

    @dbus.service.method(DBUS_PROP_IFACE, in_signature='s', out_signature='a{sv}')
    def GetAll(self, interface):
        if interface != GATT_CHRC_IFACE:
            raise InvalidArgsException()
        return self.get_properties()[GATT_CHRC_IFACE]

    @dbus.service.method(GATT_CHRC_IFACE, in_signature='aya{sv}', out_signature='')
    def WriteValue(self, value, options):
        try:
            data = json.loads(bytes(value).decode('utf-8'))
            if data.get('cmd') == 'get':
                print("Received get command, pushing full data payload...")
                push_all_cached_payloads()
        except Exception as e:
            print(f"RequestCharacteristic WriteValue error: {e}")
            traceback.print_exc()


class BLEAdvertisement(dbus.service.Object):
    def __init__(self, bus, index):
        self.path = f'/org/bluez/example/advertisement{index}'
        self.bus = bus
        self.ad_type = 'peripheral'
        self.service_uuids = [SERVICE_UUID]
        self.manufacturer_data = dbus.Dictionary({}, signature='qv')
        self.local_name = 'RasPi-HomeServer'
        self.include_tx_power = True
        dbus.service.Object.__init__(self, bus, self.path)

    def get_properties(self):
        properties = {
            'Type': self.ad_type,
            'ServiceUUIDs': dbus.Array(self.service_uuids, signature='s'),
            'LocalName': dbus.String(self.local_name),
            'IncludeTxPower': dbus.Boolean(self.include_tx_power)
        }
        return {LE_ADVERTISEMENT_IFACE: properties}

    def get_path(self):
        return dbus.ObjectPath(self.path)

    @dbus.service.method(DBUS_PROP_IFACE, in_signature='s', out_signature='a{sv}')
    def GetAll(self, interface):
        if interface != LE_ADVERTISEMENT_IFACE:
            raise InvalidArgsException()
        return self.get_properties()[LE_ADVERTISEMENT_IFACE]

    @dbus.service.method(LE_ADVERTISEMENT_IFACE, in_signature='', out_signature='')
    def Release(self):
        print(f'{self.path}: Released!')

def register_app_cb():
    print('GATT App registered successfully!')

def register_app_error_cb(error):
    print(f'Failed to register app: {str(error)}')
    mainloop.quit()

def register_ad_cb():
    print('Advertisement registered successfully!')

def register_ad_error_cb(error):
    print(f'Failed to register advertisement: {str(error)}')
    mainloop.quit()

def find_adapter(bus):
    remote_om = dbus.Interface(bus.get_object(BLUEZ_SERVICE_NAME, '/'), DBUS_OM_IFACE)
    objects = remote_om.GetManagedObjects()
    for o, props in objects.items():
        if GATT_MANAGER_IFACE in props.keys() and LE_ADVERTISING_MANAGER_IFACE in props.keys():
            return o
    return None

def read_filtered_sensor_data(path):
    with open(path, 'r') as f:
        new_data = json.load(f)
    return {
        key: round(new_data[key], 1 if key in ["temperature_c"] else 0)
        for key in ["temperature_c", "humidity_percent", "iaq", "co2_eq_ppm"]
        if key in new_data
    }


def trigger_from_file(path, reason):
    try:
        filtered_data = {
            "sensor": read_filtered_sensor_data(path)
        }
        print(f"{reason}: pushing latest sensor payload")
        return filtered_data
        
    except Exception as e:
        print(f"{reason}: failed to read or parse JSON: {e}")


def push_payloads_separately(payloads, reason):
    """Push each dictionary payload as its own BLE notification."""
    for payload in payloads:
        if not payload:
            continue
        try:
            main_char.push_data(payload)
        except Exception as e:
            print(f"{reason}: failed to push payload {payload}: {e}")
            traceback.print_exc()

def push_all_cached_payloads():
    """Push all currently cached data unconditionally (used by the get command)."""
    global cache_payload_sensor, cache_payload_weather_calendar, cache_payload_subway, cache_payload_news
    payloads = []
    payloads.append({"timestamp": datetime.now().timestamp()})
    push_payloads_separately(payloads, "get command")
    payloads.clear()  # Clear payloads list to avoid duplicate pushes in the next step

    payloads.append(cache_payload_sensor)
    payloads.append({"calendar": cache_payload_weather_calendar.get("calendar", {})})
    payloads.append({"weather": cache_payload_weather_calendar.get("weather", {})})
    payloads.append({"info": cache_payload_news})
    payloads.append({"alert": cache_payload_subway})
    push_payloads_separately(payloads, "get command")

def check_weather_and_calendar():
    from check_calendar import main as check_calendar
    from check_weather import main as check_weather

    calendar_data = check_calendar()
    weather_data = check_weather()

    combined_data = {
        "calendar": calendar_data,
        "weather": weather_data
    }
    return combined_data

def check_news_and_get_headlines():
    from check_news import main as check_news
    return check_news()

def is_valid_hhmm(value):
    try:
        datetime.strptime(value, '%H:%M')
        return True
    except ValueError:
        return False

def check_subway():
    from check_subway import main as check_subway
    return check_subway()

def on_file_changed(monitor, file, other_file, event_type):
    global last_triggered_timestamp
    global cache_payload_sensor, cache_payload_weather_calendar, cache_payload_subway, cache_payload_news

    # Print the exact event type we received for debugging
    # print(f"File event detected! Type: {event_type}")
    
    # Catch any file modification events
    if event_type == Gio.FileMonitorEvent.CHANGES_DONE_HINT:
        # payloads = []
        sensor_payload = trigger_from_file(file.get_path(), "File change trigger")
        cache_payload_sensor = sensor_payload  # Update cache with the latest sensor payload

        if datetime.now().hour in [7,8]:  # Check subway status during morning commute hours
            cache_payload_subway = check_subway()

        if datetime.now().minute == 50:  # For weather and calendar, trigger every hour at :50
            cache_payload_weather_calendar = check_weather_and_calendar()

        if datetime.now().hour in [5,17] and datetime.now().minute == 50:  # For news headlines, trigger during early morning hours
            cache_payload_news = check_news_and_get_headlines()

def on_initial_connection():
    payloads = []
    payloads.append({"timestamp": datetime.now().timestamp()})
    payloads.append(cache_payload_sensor)
    push_payloads_separately(payloads, "Sensor push on initial connection")
    payloads.clear()  # Clear payloads list to avoid duplicate pushes in the next step

    if main_char.notifying: # Only attempt to push if the ESP32 is still subscribed after the delay
        if time.localtime().tm_min == 00 and cache_payload_weather_calendar:  # If it's the right time for weather/calendar data, push it immediately on connection
            payloads.append({"calendar": cache_payload_weather_calendar.get("calendar", {})})
            payloads.append({"weather": cache_payload_weather_calendar.get("weather", {})})
        if time.localtime().tm_min == 00 and time.localtime().tm_hour in [6,18] and cache_payload_news:  # If it's the right time for news headlines, push it immediately on connection
            payloads.append({"info": cache_payload_news})
        if time.localtime().tm_hour in [7,8] and cache_payload_subway:  # If it's commute hours, push subway status immediately on connection
            payloads.append({"alert": cache_payload_subway})
        if time.localtime().tm_min == 00 and time.localtime().tm_hour in [3]:  # Only include timestamp 3:00 AM
            payloads.append({"timestamp": datetime.now().timestamp()})
        push_payloads_separately(payloads, "All info push on initial connection")

def setup_file_monitor():
    print(f"Setting up inotify watch on: {HOME_TEMP_JSON_PATH}")
    gfile = Gio.File.new_for_path(HOME_TEMP_JSON_PATH)
    monitor = gfile.monitor_file(Gio.FileMonitorFlags.NONE, None)
    monitor.connect("changed", on_file_changed)
    return monitor

def main():
    global mainloop

    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    
    adapter = find_adapter(bus)
    if not adapter:
        print('GATTManager1 or LEAdvertisingManager1 interface not found.')
        print('Ensure your Raspberry Pi Bluetooth daemon is running with --experimental')
        sys.exit(1)

    adapter_obj = bus.get_object(BLUEZ_SERVICE_NAME, adapter)
    
    # Prepare payload
    global cache_payload_sensor, cache_payload_weather_calendar, cache_payload_subway, cache_payload_news
    cache_payload_sensor = trigger_from_file(HOME_TEMP_JSON_PATH, "Initial cache population") or {}
    cache_payload_subway = check_subway()
    cache_payload_weather_calendar = check_weather_and_calendar()
    cache_payload_news = check_news_and_get_headlines()

    # Setup App & Services layer
    gatt_manager = dbus.Interface(adapter_obj, GATT_MANAGER_IFACE)
    app = BLEApplication(bus)
    service = BLEService(bus, 0, SERVICE_UUID, True)
    
    global main_char
    main_char = SensorCharacteristic(bus, 0, service)
    service.add_characteristic(main_char)
    request_char = RequestCharacteristic(bus, 1, service)
    service.add_characteristic(request_char)
    app.add_service(service)

    # Setup Advertisement layer
    ad_manager = dbus.Interface(adapter_obj, LE_ADVERTISING_MANAGER_IFACE)
    advertisement = BLEAdvertisement(bus, 0)

    # Register both App & Advertisement
    mainloop = GLib.MainLoop()
    register_options = dbus.Dictionary({}, signature='sv')
    print('Registering GATT application...')
    gatt_manager.RegisterApplication(app.get_path(), register_options,
                                     reply_handler=register_app_cb,
                                     error_handler=register_app_error_cb)

    print('Registering LE Advertisement...')
    ad_manager.RegisterAdvertisement(advertisement.get_path(), register_options,
                                     reply_handler=register_ad_cb,
                                     error_handler=register_ad_error_cb)

    def shutdown_handler(signum, frame):
        print(f"\nReceived signal {signum}, shutting down...")
        mainloop.quit()

    signal.signal(signal.SIGINT, shutdown_handler)
    signal.signal(signal.SIGTERM, shutdown_handler)

    # Attach the native inotify file watch into the GLib Event loop
    file_monitor = setup_file_monitor()

    print(
        "Raspberry Pi BLE Server running! Waiting for ESP32... "
    )
    try:
        mainloop.run()
    finally:
        print("Cleaning up BlueZ registrations...")
        try:
            ad_manager.UnregisterAdvertisement(advertisement.get_path())
            print("Advertisement unregistered.")
        except Exception as e:
            print(f"Failed to unregister advertisement: {e}")
            
        try:
            gatt_manager.UnregisterApplication(app.get_path())
            print("Application unregistered.")
        except Exception as e:
            print(f"Failed to unregister application: {e}")

if __name__ == '__main__':
    main()
