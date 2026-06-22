// src/comm/modbushelper.h — Modbus TCP/RTU 公共工具类
#ifndef MODBUSHELPER_H
#define MODBUSHELPER_H

#include <QString>
#include <QVariant>
#include <QVector>
#include <QDebug>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include "common_types.h"

extern "C" {
#include "modbus.h"
}

namespace ModbusHelper {

// ============================================================
// 地址解析："FC:寄存器地址" → (fc, regAddr, count)
// ============================================================
inline bool parseAddress(const QString &address, int &fc, int &regAddr, int &count)
{
    // 格式：功能码:起始地址[:数量]
    // 功能码 1=Coil, 2=Discrete Input, 3=Holding Register, 4=Input Register
    QStringList parts = address.split(':');
    if (parts.size() < 2) return false;

    bool ok = false;
    fc = parts[0].toInt(&ok);
    if (!ok || fc < 1 || fc > 4) return false;

    regAddr = parts[1].toInt(&ok);
    if (!ok || regAddr < 0) return false;

    // 可选的数量字段（默认1）
    count = (parts.size() >= 3) ? parts[2].toInt(&ok) : 1;
    if (parts.size() >= 3 && (!ok || count <= 0)) return false;

    return true;
}

// ============================================================
// 寄存器数组 → QVariant（读取方向，大端字节序）
// ============================================================
inline QVariant regsToVariant(const uint16_t *regs, int regCount, TagDataType dataType)
{
    if (!regs || regCount <= 0) return QVariant();

    switch (dataType) {
    case TagDataType::BOOL:
        // 线圈/离散量：单 bit
        return QVariant((regs[0] & 0x01) != 0);

    case TagDataType::BYTE:
        return QVariant(static_cast<quint8>(regs[0] & 0xFF));

    case TagDataType::WORD:
        return QVariant(regs[0]);

    case TagDataType::DWORD:
        // 大端：高16位在前
        if (regCount >= 2)
            return QVariant(((uint32_t)regs[0] << 16) | regs[1]);
        return QVariant((uint32_t)regs[0]);

    case TagDataType::INT:
        return QVariant(static_cast<qint16>(regs[0]));

    case TagDataType::DINT:
        // 大端 int32
        if (regCount >= 2) {
            uint32_t raw = ((uint32_t)regs[0] << 16) | regs[1];
            return QVariant(static_cast<qint32>(raw));
        }
        return QVariant(static_cast<qint32>(regs[0]));

    case TagDataType::REAL:
        // IEEE 754 单精度浮点，大端
        if (regCount >= 2) {
            uint32_t raw = ((uint32_t)regs[0] << 16) | regs[1];
            float f;
            std::memcpy(&f, &raw, 4);
            return QVariant(f);
        }
        return QVariant(0.0f);

    default:
        return QVariant(regs[0]);
    }
}

// ============================================================
// QVariant → uint16_t 数组（写入方向，大端字节序）
// ============================================================
inline QVector<uint16_t> variantToRegs(const QVariant &value, TagDataType dataType)
{
    QVector<uint16_t> result;

    if (!value.isValid()) return result;

    switch (dataType) {
    case TagDataType::BOOL:
        result.append(value.toBool() ? 0xFF00 : 0x0000);
        break;

    case TagDataType::BYTE:
    case TagDataType::WORD:
        result.append(static_cast<uint16_t>(value.toUInt() & 0xFFFF));
        break;

    case TagDataType::DWORD: {
        uint32_t raw = value.toUInt();
        result.append(static_cast<uint16_t>((raw >> 16) & 0xFFFF));
        result.append(static_cast<uint16_t>(raw & 0xFFFF));
        break;
    }

    case TagDataType::INT: {
        result.append(static_cast<uint16_t>(value.toInt() & 0xFFFF));
        break;
    }

    case TagDataType::DINT: {
        uint32_t raw = static_cast<uint32_t>(value.toInt());
        result.append(static_cast<uint16_t>((raw >> 16) & 0xFFFF));
        result.append(static_cast<uint16_t>(raw & 0xFFFF));
        break;
    }

    case TagDataType::REAL: {
        float f = value.toFloat();
        uint32_t raw;
        std::memcpy(&raw, &f, 4);
        result.append(static_cast<uint16_t>((raw >> 16) & 0xFFFF));
        result.append(static_cast<uint16_t>(raw & 0xFFFF));
        break;
    }

    default:
        result.append(static_cast<uint16_t>(value.toUInt() & 0xFFFF));
        break;
    }

    return result;
}

// ============================================================
// libmodbus 读取封装（按功能码分发）
// 返回实际读取数量，<0 表示错误
// ============================================================
inline int readRegisters(modbus_t *ctx, int fc, int addr, int count, uint8_t *dest)
{
    if (!ctx || !dest) return -1;

    switch (fc) {
    case 1: // FC1 Read Coils
        return modbus_read_bits(ctx, addr, count, dest);
    case 2: // FC2 Read Discrete Inputs
        return modbus_read_input_bits(ctx, addr, count, dest);
    case 3: // FC3 Read Holding Registers
        return modbus_read_registers(ctx, addr, count, (uint16_t*)dest);
    case 4: // FC4 Read Input Registers
        return modbus_read_input_registers(ctx, addr, count, (uint16_t*)dest);
    default:
        return -1;
    }
}

// ============================================================
// libmodbus 单个寄存器写入
// ============================================================
inline int writeRegister(modbus_t *ctx, int fc, int addr, uint16_t value)
{
    if (!ctx) return -1;

    switch (fc) {
    case 1: // FC5 Write Single Coil
        return modbus_write_bit(ctx, addr, value ? 1 : 0);
    case 3: // FC6 Write Single Register
        return modbus_write_register(ctx, addr, value);
    default:
        return -1;
    }
}

// ============================================================
// libmodbus 多个寄存器写入
// ============================================================
inline int writeRegisters(modbus_t *ctx, int fc, int addr, int count, const uint16_t *data)
{
    if (!ctx || !data) return -1;

    switch (fc) {
    case 1: // FC15 Write Multiple Coils
        return modbus_write_bits(ctx, addr, count, (const uint8_t*)data);
    case 3: // FC16 Write Multiple Registers
        return modbus_write_registers(ctx, addr, count, data);
    default:
        return -1;
    }
}

} // namespace ModbusHelper

#endif // MODBUSHELPER_H
