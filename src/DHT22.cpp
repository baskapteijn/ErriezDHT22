/*
 * MIT License
 *
 * Copyright (c) 2018 Erriez
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*!
 * \brief DHT22 (AM2303) Humidity and Temperature sensor example for Arduino
 * \details Source: https://github.com/Erriez/ErriezDHT22
 * \file DHT22.cpp
 */

#include "DHT22.h"

/*!
 * \brief Constructor DHT22 sensor.
 * \param pin Data pin sensor.
 */
DHT22::DHT22(uint8_t pin)
{
    // Store data pin
    _pin = pin;

    // For AVR targets only:
    // Calculate bit and port register for faster pin reads and writes instead
    // of using the slow digitalRead() function
#ifdef __AVR
    _bit = digitalPinToBitMask(pin);
    _port = digitalPinToPort(pin);
#endif

    // 1 ms timeout for reading data from DHT22 sensor
    _maxCycles = microsecondsToClockCycles(1000);
}

/*!
 * \brief Initialize sensor.
 * \details
 *      Call this function from setup().\n
 *
 *      - Connect an external 3k3..10k pull-up resistor between the DAT and VCC pins only when:\n
 *          - using a AM2302 sensor without a DT22 breakout PCB\n
 *             AND\n
 *          - the MCU IO pin has no built-in or external pull-up resistor.\n
 *      - The DHT22 breakout PCB contains a 3k3 pull-up resistor between DAT and VCC.\n
 *      - Please refer to the MCU datasheet or board schematic for more information about IO pin\n
 *        pull-up resistors.
 */
void DHT22::begin()
{
    // Try to enable internal pin pull-up resistor when available
    pinMode(_pin, INPUT_PULLUP);

    // Initialize last measurement timestamp with negative interval to allow
    // a new measurement
    _lastMeasurementTimestamp = (uint32_t)-DHT22_MIN_READ_INTERVAL;
}

/*!
 * \brief Check if new temperature or humidity read is allowed.
 * \details
 *      The application should call this function and check if a new temperature
 *      and humidity can be read to prevent too fast sensor reads.
 * \return
 *      true:  Available, interval between sensor reads >= 2000 ms.
 *      false: Not available, interval between sensor reads too short.
 */
bool DHT22::available()
{
    if ((millis() - _lastMeasurementTimestamp) < 2000) {
        // Interval between sensor reads too short
        return false;
    }

    // Ready to readSensorData humidity and temperature
    return true;
}

/*!
 * \brief Read temperature from sensor.
 * \details
 *      Returns the actual temperature, or a cached temperature when read
 *      interval is too short.
 * \return
 *      Signed temperature with last digit after the point
 *      ~0: An error occurred
 */
int16_t DHT22::readTemperature()
{
    int16_t temperature = ~0;

    // Read data from sensor
    if (readSensorData()) {
        // Calculate signed temperature
        temperature = ((_data[2] & 0x7F) << 8) | _data[3];
        if (_data[2] & 0x80) {
            temperature *= -1;
        }
    }

    return temperature;
}

/*!
 * \brief Read humidity from sensor.
 * \return
 *      Signed humidity with last digit after the point.
 *      ~0: An error occurred
 */
int16_t DHT22::readHumidity()
{
    int16_t humidity = ~0;

    // Read data from sensor
    if (readSensorData()) {
        // Calculate humidity
        humidity = (_data[0] << 8) | _data[1];
    }

    return humidity;
}

//------------------------------------------------------------------------------
// Private functions
//------------------------------------------------------------------------------
/*!
 * \brief Read data from sensor.
 * \details
 *      5 sensor bytes will be read when interval between previous read
 *      >= 2000 ms.
 * \return
 *      true:  Last conversion was successful.
 *      false: Last conversion was unsuccessful.
 */
bool DHT22::readSensorData()
{
    if (available() == false) {
        // Return last measurement status
        return _statusLastMeasurement;
    }

    // Store last conversion timestamp
    _lastMeasurementTimestamp = millis();

    // Mark current measurement as invalid
    _statusLastMeasurement = false;

    // Generate sensor start pulse
    if (generateStart() != true) {
        DEBUG_PRINTLN(F("DHT22: Start error"));
        return false;
    }

    // Read 5 Bytes data from sensor
    if (readBytes() != true) {
        DEBUG_PRINTLN(F("DHT22: Read error"));
        return false;
    }

    // Check data parity
    if (((_data[0] + _data[1] + _data[2] + _data[3]) & 0xFF) != _data[4]) {
        DEBUG_PRINTLN(F("DHT22: Parity error"));
        return false;
    }

    // Mark measurement as successful
    _statusLastMeasurement = true;

    return _statusLastMeasurement;
}

/*!
 * \brief Generate start pulses to start data read.
 * \return
 *      true:  Success, continue with reading temperature and humidity bytes.
 *      false: Failure, the sensor did not respond.
 */
bool DHT22::generateStart()
{
    // Data pin high (pull-up)
    digitalWrite(_pin, HIGH);
    delay(10);

    // Change data pin to output, low, followed by high
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, LOW);
    delay(20);
    digitalWrite(_pin, HIGH);
    delayMicroseconds(40);

    // Data pin to input
    pinMode(_pin, INPUT_PULLUP);
    delayMicroseconds(10);

    // Check data pin timing low
    if (measurePulseWidth(LOW) == 0) {
        return false;
    }

    // Check data pin timing high
    if (measurePulseWidth(HIGH) == 0) {
        return false;
    }

    // Sensor start successfully generated
    return true;
}

/*!
 * \brief Read humidity, temperature and parity bytes from sensor.
 * \details
 *      Global interrupts are disabled during pin measurement, because the
 *      timing in micro seconds is very critical.
 * \return
 *      true:  Success
 *      false: Incorrect timing sensor data pin received.
 */
bool DHT22::readBytes()
{
    // Disable interrupts during data transfer
    noInterrupts();

    // Measure and store pulse width of each bit
    for (int i = 0; i < (DHT22_NUM_DATA_BITS * 2); i += 2) {
        cycles[i] = measurePulseWidth(LOW);
        cycles[i + 1] = measurePulseWidth(HIGH);
    }

    // Enable interrupts
    interrupts();

    // Clear data buffer
    memset(_data, 0, sizeof(_data));

    // Convert pulse width to data bit
    for (int i = 0; i < DHT22_NUM_DATA_BITS; ++i) {
        uint32_t lowCycles = cycles[2 * i];
        uint32_t highCycles = cycles[(2 * i) + 1];

        // Check valid bit timing
        if ((lowCycles == 0) || (highCycles == 0)) {
            return false;
        }

        // Calculate byte index in data array
        int byteIndex = i / 8;

        // Store bit
        _data[byteIndex] <<= 1;
        if (highCycles > lowCycles) {
            _data[byteIndex] |= 1;
        }
    }

    return true;
}

/*!
 * \brief Measure data pin pulse width.
 * \param level Measure data signal low or high.
 * \return
 *      Sensor data pin timing in us.
 *      0: Timeout
 */
uint32_t DHT22::measurePulseWidth(uint8_t level)
{
    uint32_t count = 0;

#ifdef __AVR
    while ((*portInputRegister(_port) & _bit) == (level ? _bit : 0)) {
      if (count++ >= _maxCycles) {
        // Timeout
        return 0;
      }
    }

#else
    while (digitalRead(_pin) == level) {
        if (count++ >= _maxCycles) {
            // Timeout
            return 0;
        }
    }
#endif

    return count;
}
