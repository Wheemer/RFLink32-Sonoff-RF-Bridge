    void delayLongPulse(unsigned long duration_us)
    {
      if(duration_us <= 5000UL) {
        delayMicroseconds(duration_us);
        return;
      }

      // Long raw-code marks or spaces can be 80-110ms. Holding interrupts off
      // for either level starves ESP8266 Wi-Fi/watchdog and can reboot the bridge.
      interrupts();
      while(duration_us >= 1000UL) {
        unsigned long chunk = duration_us > 16000UL ? 16000UL : duration_us;
        delayMicroseconds(chunk);
        duration_us -= chunk;
        yield();
        ESP.wdtFeed();
      }
      if(duration_us > 0)
        delayMicroseconds(duration_us);
      noInterrupts();
    }

    void RawSendRF(RawSignalStruct *signal)
    {
      int x;
      const uint32_t started_us = micros();
      unsigned long started_ms = millis();
      runtime::txStartedCount++;
      runtime::txLastStartedMs = started_ms;
      runtime::txLastFinishedMs = 0;
      runtime::txLastElapsedMs = 0;
      runtime::txLastPulses = signal->Number;
      runtime::txLastRepeats = signal->Repeats;
      runtime::txLastDelay = signal->Delay;
      runtime::txLastMultiply = signal->Multiply;
      runtime::txLastError[0] = 0;

      Radio::set_Radio_mode(Radio::States::Radio_TX);

      for (uint16_t y = 0; y <= signal->Repeats; y++)
      { // repeat transmitting the RF code
        x = 1;
        noInterrupts();
        while (x < signal->Number)
        {
          unsigned long high_us = (unsigned long)signal->Pulses[x++] * signal->Multiply;
          unsigned long low_us = (unsigned long)signal->Pulses[x++] * signal->Multiply;
          digitalWrite(Radio::pins::TX_DATA, HIGH);
          delayLongPulse(high_us);
          digitalWrite(Radio::pins::TX_DATA, LOW);
          delayLongPulse(low_us);
        }
        interrupts();
        if (y != signal->Repeats)
          delay(signal->Delay); // gap between repeats, outside the noInterrupts region
      }

      Radio::set_Radio_mode(Radio::States::Radio_RX);
      runtime::txFinishedCount++;
      runtime::txLastFinishedMs = millis();
      // ESP8266 millis() stops while interrupts are held for short RF pulses.
      // The hardware microsecond clock continues, so it is the truthful TX duration.
      runtime::txLastElapsedMs = (micros() - started_us) / 1000UL;
    }
