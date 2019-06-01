# esp-wifi-temp-sensor
Wifi enabled temperature sensor for BME280 and ESP8266 (Wemos D1 mini)

Simple Wifi-enabled temperature sensor for perioidically reading data from BME280 sensor. Data is uploaded to Bebotte cloud service.
The Bebotte key and channel name (different for each sensor) need to be configured while initializing the sensor. To enable the
initialization mode (and format any saved data) the D5 pin needs to be set HIGH. The solution is probably not the most energy efficient
but the power consumption can be controlled by setting the upload interval to a higher number.

Data collected
- Temperature
- Humidity
- Pressure
- Altitude
- Voltage (input)
