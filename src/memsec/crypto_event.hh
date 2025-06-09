#include "crypto_ctrl.hh"

namespace gem5 {

class AESEncryptEvent : public Event
{
  private:
    CryptoCtrl *ctrl;
    PacketPtr pkt;
  public:
    AESEncryptEvent(CryptoCtrl *ctrl, PacketPtr pkt) :
        Event(Default_Pri, AutoDelete), ctrl(ctrl), pkt(pkt)
    { }
    void process() override {
        // process packet by using callback
        ctrl->AESEncrypt(pkt);
    }
};

class AESDecryptEvent : public Event
{
  private:
    CryptoCtrl *ctrl;
    PacketPtr pkt;
  public:
    AESDecryptEvent(CryptoCtrl *ctrl, PacketPtr pkt) :
        Event(Default_Pri, AutoDelete), ctrl(ctrl), pkt(pkt)
    { }
    void process() override {
        // process packet by using callback
        ctrl->AESDecrypt(pkt);
    }
};

}
