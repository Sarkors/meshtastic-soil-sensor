#pragma once
#include "SinglePortModule.h"
#include "configuration.h"

class GPIOPinTestModule : public SinglePortModule
{
  public:
    GPIOPinTestModule();

  protected:
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
};

extern GPIOPinTestModule *gpioPinTestModule; 	