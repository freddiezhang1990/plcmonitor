#!/usr/bin/env python3
"""
auto_mapping_from_svg.py  v2.0
从 SVG 文件自动生成 mapping.json 骨架（适配多设备 tagKey 架构）
用法: python auto_mapping_from_svg.py <input.svg> <output.json> [--device PLC_01]
"""

import sys
import json
import xml.etree.ElementTree as ET

NS = {'svg': 'http://www.w3.org/2000/svg'}


def infer_device_type(element_id):
    id_upper = element_id.upper()
    if id_upper.startswith('PUMP'):   return 'pump'
    if id_upper.startswith('VALVE'):
        return 'valve_regulating' if 'REG' in id_upper else 'valve_onoff'
    if id_upper.startswith('TANK'):   return 'tank'
    if id_upper.startswith('FAN'):    return 'fan'
    if id_upper.startswith('COMP'):   return 'compressor'
    if id_upper.startswith('PIPE'):   return 'pipe'
    if id_upper.startswith('MOTOR'):  return 'motor'
    if id_upper.startswith('SENSOR'): return 'sensor'
    return 'unknown'


def build_entry(svg_id, device_id):
    dev_type = infer_device_type(svg_id)
    tag_name = svg_id.lower()
    tag_key = f"{device_id}/{tag_name}"

    can_control = dev_type in ('pump', 'fan', 'compressor', 'motor',
                                'valve_regulating', 'valve_onoff')

    entry = {
        "svgId": svg_id,
        "tagKey": tag_key,
        "labelName": svg_id,
        "deviceType": dev_type,
        "clickAction": "showStatus",
        "dblClickAction": "showControlPanel" if can_control else "showDetail",
        "tooltip": svg_id,
        "contextMenu": [],
        "controlPanel": None,
        "statusBinding": []
    }

    if dev_type in ('pump', 'fan', 'compressor', 'motor'):
        entry["statusBinding"] = [
            {"tagKey": tag_key, "type": "color",
             "trueColor": "#00cc00", "falseColor": "#cc0000"}
        ]
        entry["contextMenu"] = [
            {"text": "启动", "action": "writeTag",
             "params": {"tagKey": f"{device_id}/{tag_name}_cmd", "value": 1},
             "confirm": True, "confirmMsg": f"确认启动 {svg_id}？"},
            {"text": "停止", "action": "writeTag",
             "params": {"tagKey": f"{device_id}/{tag_name}_cmd", "value": 0},
             "confirm": True, "confirmMsg": f"确认停止 {svg_id}？"},
            {"text": "详细信息", "action": "showDetail"}
        ]
        entry["controlPanel"] = {
            "type": "switch",
            "writeTagKey": f"{device_id}/{tag_name}_cmd",
            "feedbackTagKey": tag_key,
            "onLabel": "启动", "offLabel": "停止",
            "timeout": 5000, "monitorTags": []
        }
    elif dev_type in ('valve_regulating', 'valve_onoff'):
        is_reg = dev_type == 'valve_regulating'
        entry["statusBinding"] = [
            {"tagKey": tag_key, "type": "dynamicColor" if is_reg else "color",
             "calc": "valveColorByPosition" if is_reg else "",
             "trueColor": "#00cc00" if not is_reg else "",
             "falseColor": "#666666" if not is_reg else ""}
        ]
        cp_type = "analog" if is_reg else "switch"
        entry["controlPanel"] = {
            "type": cp_type,
            "writeTagKey": f"{device_id}/{tag_name}_cmd",
            "feedbackTagKey": tag_key,
            "unit": "%" if is_reg else "",
            "min": 0, "max": 100, "step": 1, "timeout": 5000
        }
        if not is_reg:
            entry["contextMenu"] = [
                {"text": "打开", "action": "writeTag",
                 "params": {"tagKey": f"{device_id}/{tag_name}_cmd", "value": 1},
                 "confirm": True},
                {"text": "关闭", "action": "writeTag",
                 "params": {"tagKey": f"{device_id}/{tag_name}_cmd", "value": 0},
                 "confirm": True}
            ]

    return entry


def main(svg_path, output_path, device_id="PLC_01"):
    tree = ET.parse(svg_path)
    root = tree.getroot()

    # 查找所有带 id 的元素（g 分组、rect、circle、path 等）
    mapping = {
        "projectName": "",
        "diagramFile": svg_path,
        "version": "2.0",
        "defaultDeviceId": device_id,
        "mappings": []
    }

    for elem in root.iter():
        svg_id = elem.get('id', '').strip()
        if not svg_id or svg_id.startswith('_'):
            continue
        # 跳过 SVG 根元素自身的 id
        if elem.tag.endswith('}svg') or elem.tag == 'svg':
            continue
        # 去重
        for m in mapping['mappings']:
            if m['svgId'] == svg_id:
                break
        else:
            mapping['mappings'].append(build_entry(svg_id, device_id))

    with open(output_path, 'w', encoding='utf-8') as f:
        json.dump(mapping, f, indent=2, ensure_ascii=False)

    print(f"生成 {len(mapping['mappings'])} 个映射条目 → {output_path}")
    print(f"默认设备 ID: {device_id}")
    print("下一步：根据实际标签名修改每条 'tagKey' 字段（格式：设备ID/标签名）")
    print("可在项目目录下运行: python tools/auto_mapping_from_svg.py <你的.svg> <输出.json>")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("用法: python auto_mapping_from_svg.py <input.svg> <output.json> [--device PLC_01]")
        print("示例: python auto_mapping_from_svg.py process.svg mapping.json --device PLC_S7_01")
        sys.exit(1)

    device = "PLC_01"
    for i, arg in enumerate(sys.argv):
        if arg == '--device' and i + 1 < len(sys.argv):
            device = sys.argv[i + 1]

    main(sys.argv[1], sys.argv[2], device)
