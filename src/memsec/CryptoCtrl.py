from m5.params import *
from m5.SimObject import SimObject


class CryptoCtrl(SimObject):
    type = "CryptoCtrl"
    cxx_header = "memsec/crypto_ctrl.hh"
    cxx_class = "gem5::CryptoCtrl"

    aes_enc_cycles = Param.Cycles(
        336, "AES-CTR encryption operation cycle delay per block"
    )
    aes_dec_cycles = Param.Cycles(
        216, "AES-CTR decryption operation cycle delay per block"
    )
    aes_enc_ii = Param.Cycles(
        336, "AES-CTR encryption operation initiation interval per block"
    )
    aes_dec_ii = Param.Cycles(
        216, "AES-CTR decryption operation initiation interval per block"
    )

    cpu_side_port = ResponsePort("CPU side port")
    mem_side_port = RequestPort("memory side port")
