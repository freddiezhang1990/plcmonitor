import sys

# Read original .ui content from backup, add protocol fields
with open(r'D:\plcmonitor20260610\src\ui\dialogs\DeviceConfigDialog.ui', 'r', encoding='utf-8') as f:
    content = f.read()

# Add protocol combo box to deviceForm
# Find the deviceForm layout start
old_device_form_start = '''<layout class="QFormLayout" name="deviceForm">
        <property name="fieldGrowthPolicy">
         <enum>QFormLayout::FieldGrowthPolicy::AllNonFixedFieldsGrow</enum>
        </property>
        <item row="0" column="0">
         <widget class="QLabel" name="labelDeviceId">
          <property name="text">
           <string>设备ID：</string>
          </property>
         </widget>
        </item>'''

protocol_insert = '''<layout class="QFormLayout" name="deviceForm">
        <property name="fieldGrowthPolicy">
         <enum>QFormLayout::FieldGrowthPolicy::AllNonFixedFieldsGrow</enum>
        </property>
        <item row="0" column="0">
         <widget class="QLabel" name="labelProtocol">
          <property name="text">
           <string>\u534f\u8bae\uff1a</string>
          </property>
         </widget>
        </item>
        <item row="0" column="1">
         <widget class="QComboBox" name="protocolCombo">
          <item>
           <property name="text">
            <string>S7 (\u897f\u95e8\u5b50 S7-200/300/400/1200/1500)</string>
           </property>
          </item>
          <item>
           <property name="text">
            <string>ModbusTCP</string>
           </property>
          </item>
          <item>
           <property name="text">
            <string>ModbusRTU</string>
           </property>
          </item>
         </widget>
        </item>
        <item row="1" column="0">
         <widget class="QLabel" name="labelDeviceId">
          <property name="text">
           <string>\u8bbe\u5907ID\uff1a</string>
          </property>
         </widget>
        </item>'''

if 'deviceForm' not in content:
    print("ERROR: deviceForm not found in UI file!")
    sys.exit(1)

content = content.replace(old_device_form_start, protocol_insert)

# Add protocol handling code to the C++ file
print("UI file updated with protocol fields")

with open(r'D:\plcmonitor20260610\src\ui\dialogs\DeviceConfigDialog.ui', 'w', encoding='utf-8') as f:
    f.write(content)
print("Done")
