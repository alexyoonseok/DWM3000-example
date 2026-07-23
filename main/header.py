def generate_dw3000_header(
    trans_type: str,
    base_addr: int = 0,
    sub_addr: int = 0,
    is_write: bool = True,
    cmd_id: int = 0,
    mask_mode: str = "16bit",
) -> list[str]:
    """Generates DW3000 SPI Header Bytes based on register offsets and transaction mode.

    Parameters:
      trans_type: "full", "short", "fast", "mask"
      base_addr: Register Base Address (0 to 31)
      sub_addr: Register Sub-Address Offset (0 to 127)
      is_write: True for Write, False for Read
      cmd_id: Fast Command ID (0 to 31)
      mask_mode: Masked Write Mode ("8bit", "16bit", "32bit")
    """
    rw_bit = 1 if is_write else 0

    if trans_type.lower() == "fast":
        # Fast Command Transaction (1 Byte)
        # Format: [1] [0] [Fast Command 5 bits] [1]
        byte0 = (1 << 7) | (0 << 6) | ((cmd_id & 0x1F) << 1) | 1
        return [f"0x{byte0:02X}"]

    elif trans_type.lower() == "short":
        # Short Addressed Transaction (1 Byte)
        # Format: [RD/WR] [0] [Base Addr 5 bits] [0]
        byte0 = (rw_bit << 7) | (0 << 6) | ((base_addr & 0x1F) << 1) | 0
        return [f"0x{byte0:02X}"]

    elif trans_type.lower() == "full":
        # Full Addressed Transaction (2 Bytes)
        # Byte 0: [RD/WR] [1] [Base Addr 5 bits] [Sub-addr Bit 6]
        # Byte 1: [Sub-addr Bits 5:0] [M1=0] [M0=0]
        sub_bit_6 = (sub_addr >> 6) & 0x01
        sub_bits_5_0 = sub_addr & 0x3F

        byte0 = (rw_bit << 7) | (1 << 6) | ((base_addr & 0x1F) << 1) | sub_bit_6
        byte1 = (sub_bits_5_0 << 2) | 0x00  # Mode 00 for standard R/W

        return [f"0x{byte0:02X}", f"0x{byte1:02X}"]

    elif trans_type.lower() == "mask":
        # Masked Write Transaction (2 Bytes Header)
        # Modes: 8-bit (01), 16-bit (10), 32-bit (11)
        mode_map = {"8bit": 0b01, "16bit": 0b10, "32bit": 0b11}
        m_bits = mode_map.get(mask_mode.lower(), 0b10)

        sub_bit_6 = (sub_addr >> 6) & 0x01
        sub_bits_5_0 = sub_addr & 0x3F

        byte0 = (1 << 7) | (1 << 6) | ((base_addr & 0x1F) << 1) | sub_bit_6
        byte1 = (sub_bits_5_0 << 2) | m_bits

        return [f"0x{byte0:02X}", f"0x{byte1:02X}"]

    else:
        raise ValueError("Invalid transaction type!")


# =================================================================
# VERIFICATION EXAMPLES (Running the script)
# =================================================================
if __name__ == "__main__":
    print("--- DW3000 SPI Header Calculator ---\n")

    # 1. SYS_ENABLE_0 (Base 0x00, Sub 0x3C) - READ
    h1 = generate_dw3000_header(
        "full", base_addr=0x00, sub_addr=0x3C, is_write=False
    )
    print(f"1. READ SYS_ENABLE_0 (0x00:3C):  {h1}")

    # 2. SYS_ENABLE_0 (Base 0x00, Sub 0x3C) - WRITE
    h2 = generate_dw3000_header(
        "full", base_addr=0x00, sub_addr=0x3C, is_write=True
    )
    print(f"2. WRITE SYS_ENABLE_0 (0x00:3C): {h2}")

    # 3. TX_ANTD (Base 0x01, Sub 0x04) - WRITE
    h3 = generate_dw3000_header(
        "full", base_addr=0x01, sub_addr=0x04, is_write=True
    )
    print(f"3. WRITE TX_ANTD (0x01:04):       {h3}")

    # 4. CIA_CONF (Base 0x0E, Sub 0x00) - WRITE
    h4 = generate_dw3000_header(
        "full", base_addr=0x0E, sub_addr=0x00, is_write=True
    )
    print(f"4. WRITE CIA_CONF (0x0E:00):      {h4}")

    # 5. TXSTRT Fast Command (Cmd ID 0x01)
    h5 = generate_dw3000_header("fast", cmd_id=0x01)
    print(f"5. FAST CMD TXSTRT (0x01):       {h5}")

    # 6. TXFRS and RXFCG (Base 0x00, Sub 0x3C) - WRITE
    h6 = generate_dw3000_header(
        "mask", base_addr=0x00, sub_addr=0x3C, is_write=True
    )
    print(f"6. MASK TXFRS and RXFCG (0x00:3C):       {h6}")

    # 7. CHAN_CTRL (Base 0x01, Sub 0x14) - WRITE
    h7 = generate_dw3000_header(
        "full", base_addr=0x01, sub_addr=0x14, is_write=True
    )
    print(f"7. WRITE CHAN_CTRL (0x00:14):      {h7}")

    # 8. TX_FCTRL (Base 0x00, Sub 0x24) - WRITE
    h8 = generate_dw3000_header(
        "full", base_addr=0x00, sub_addr=0x24, is_write=True
    )
    print(f"8. WRITE TX_FCTRL (0x00:24):      {h8}")

    # 9. SYS_STATUS (Base 0x00, Sub 0x44) - READ
    h9 = generate_dw3000_header(
        "full", base_addr=0x00, sub_addr=0x44, is_write=False
    )
    print(f"9. READ SYS_STATUS (0x00:44):      {h9}")

    # 10. SYS_STATUS (Base 0x00, Sub 0x44) - WRITE
    h10 = generate_dw3000_header(
        "full", base_addr=0x00, sub_addr=0x44, is_write=True
    )
    print(f"10. WRITE SYS_STATUS (0x00:44):      {h10}")