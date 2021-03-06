#include <cstring>

#include "fnSystem.h"
#include "keys.h"


/*
KeyManager::KeyManager()
{
    //memset(mButtonActive, 0, sizeof(mButtonActive));
    //memset(mLongPressActive, 0, sizeof(mLongPressActive));
    //memset(mButtonTimer, 0, sizeof(mButtonTimer));
    //mButtonPin[eKey::BOOT_KEY] = PIN_BOOT_KEY;
    //mButtonPin[eKey::OTHER_KEY] = PIN_OTHER_KEY;
}
*/

void KeyManager::setup()
{
    //pinMode(PIN_BOOT_KEY, INPUT);
    //pinMode(PIN_OTHER_KEY, INPUT);
    fnSystem.set_pin_mode(PIN_BOOT_KEY, PINMODE_INPUT);
    fnSystem.set_pin_mode(PIN_OTHER_KEY, PINMODE_INPUT);
}

bool KeyManager::keyCurrentlyPressed(eKey key)
{
    return (fnSystem.digital_read(mButtonPin[key]) == DIGI_LOW);
}

eKeyStatus KeyManager::getKeyStatus(eKey key)
{
    eKeyStatus result = eKeyStatus::RELEASED;

    if (fnSystem.digital_read(mButtonPin[key]) == DIGI_LOW)
    {
        if (mButtonActive[key] == false)
        {
            mButtonActive[key] = true;
            mButtonTimer[key] = fnSystem.millis();
        }
        if ((fnSystem.millis() - mButtonTimer[key] > LONGPRESS_TIME) && (mLongPressActive[key] == false))
        {
            mLongPressActive[key] = true;
            // long press detected
            result = eKeyStatus::LONG_PRESSED;
        }
    }
    else
    {
        if (mButtonActive[key] == true)
        {
            if (mLongPressActive[key] == true)
            {
                mLongPressActive[key] = false;
                // long press released
            }
            else
            {
                // short press released
                result = eKeyStatus::SHORT_PRESSED;
            }
            mButtonActive[key] = false;
        }
    }
    return result;
}
