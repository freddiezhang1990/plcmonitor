import codecs

# Full UI file content with protocol fields added
content = u'''<?xml version="1.0" encoding="UTF-8"?>
<ui version="4.0">
 <class>DeviceConfigDialog</class>
 <widget class="QDialog" name="DeviceConfigDialog">
  <property name="geometry">
   <rect>
    <x>0</x>
    <y>0</y>
    <width>1100</width>
    <height>800</height>
   </rect>
  </property>
  <property name="windowTitle">
   <string>\u8bbe\u5907\u4e0e\u6807\u7b7e\u914d\u7f6e</string>
  </property>
  <layout class="QVBoxLayout" name="verticalLayout">
   <item>
    <layout class="QHBoxLayout" name="topToolBar">
     <item>
      <widget class="QLabel" name="viewLabel">
       <property name="text"><string>\u5f53\u524d\u89c6\u56fe\uff1a</string></property>
      </widget>
     </item>
     <item>
      <widget class="QComboBox" name="viewCombo">
       <item><property name="text"><string>\u5b9e\u65f6\u6570\u636e</string></property></item>
       <item><property name="text"><string>\u8d8b\u52bf\u56fe\u8868</string></property></item>
       <item><property name="text"><string>\u5de5\u827a\u603b\u8c8c</string></property></item>
       <item><property name="text"><string>\u5386\u53f2\u8d8b\u52bf</string></property></item>
      </widget>
     </item>
     <item>
      <widget class="QPushButton" name="applyBtn">
       <property name="enabled"><bool>false</bool></property>
       <property name="text"><string>\u5e94\u7528\u9009\u62e9</string></property>
      </widget>
     </item>
     <item><spacer name="horizontalSpacer"><property name="orientation"><enum>Qt::Orientation::Horizontal</enum></property></spacer></item>
     <item><widget class="QPushButton" name="addDeviceBtn"><property name="text"><string>\u6dfb\u52a0\u8bbe\u5907</string></property></widget></item>
     <item><widget class="QPushButton" name="editDeviceBtn"><property name="text"><string>\u7f16\u8f91\u8bbe\u5907</string></property></widget></item>
     <item><widget class="QPushButton" name="deleteDeviceBtn"><property name="text"><string>\u5220\u9664\u8bbe\u5907</string></property></widget></item>
     <item><widget class="QPushButton" name="addGroupBtn"><property name="text"><string>\u6dfb\u52a0\u5206\u7ec4</string></property></widget></item>
     <item><widget class="QPushButton" name="addTagBtn"><property name="text"><string>\u6dfb\u52a0\u6807\u7b7e</string></property></widget></item>
     <item><widget class="QPushButton" name="editTagBtn"><property name="text"><string>\u7f16\u8f91\u6807\u7b7e</string></property></widget></item>
     <item><widget class="QPushButton" name="deleteTagBtn"><property name="text"><string>\u5220\u9664\u6807\u7b7e</string></property></widget></item>
    </layout>
   </item>
   <item>
    <widget class="QSplitter" name="mainSplitter">
     <property name="orientation"><enum>Qt::Orientation::Horizontal</enum></property>
     <widget class="QTreeView" name="treeView">
      <property name="selectionBehavior"><enum>QAbstractItemView::SelectionBehavior::SelectRows</enum></property>
      <property name="indentation"><number>20</number></property>
      <property name="uniformRowHeights"><bool>true</bool></property>
      <attribute name="headerDefaultSectionSize"><number>200</number></attribute>
     </widget>
     <widget class="QStackedWidget" name="propertyStack">
      <widget class="QWidget" name="emptyPage"/>
      <widget class="QWidget" name="devicePropertyPage">
       <layout class="QFormLayout" name="deviceForm">
        <property name="fieldGrowthPolicy"><enum>QFormLayout::FieldGrowthPolicy::AllNonFixedFieldsGrow</enum></property>
        <item row="0" column="0"><widget class="QLabel" name="labelProtocol"><property name="text"><string>\u534f\u8bae\uff1a</string></property></widget></item>
        <item row="0" column="1"><widget class="QComboBox" name="protocolCombo"><item><property name="text"><string>S7 (\u897f\u95e8\u5b50 S7-200/300/400/1200/1500)</string></property></item><item><property name="text"><string>ModbusTCP</string></property></item><item><property name="text"><string>ModbusRTU</string></property></item></widget></item>
        <item row="1" column="0"><widget class="QLabel" name="labelDeviceId"><property name="text"><string>\u8bbe\u5907ID\uff1a</string></property></widget></item>
        <item row="1" column="1"><widget class="QLineEdit" name="deviceIdEdit"/></item>
        <item row="2" column="0"><widget class="QLabel" name="labelDeviceName"><property name="text"><string>\u8bbe\u5907\u540d\u79f0\uff1a</string></property></widget></item>
        <item row="2" column="1"><widget class="QLineEdit" name="deviceNameEdit"/></item>
        <item row="3" column="0"><widget class="QLabel" name="labelIP"><property name="text"><string>IP\u5730\u5740\uff1a</string></property></widget></item>
        <item row="3" column="1"><widget class="QLineEdit" name="ipEdit"/></item>
        <item row="4" column="0"><widget class="QLabel" name="labelRack"><property name="text"><string>Rack\uff1a</string></property></widget></item>
        <item row="4" column="1"><widget class="QSpinBox" name="rackSpinBox"><property name="minimum"><number>0</number></property><property name="maximum"><number>31</number></property></widget></item>
        <item row="5" column="0"><widget class="QLabel" name="labelSlot"><property name="text"><string>Slot\uff1a</string></property></widget></item>
        <item row="5" column="1"><widget class="QSpinBox" name="slotSpinBox"><property name="minimum"><number>0</number></property><property name="maximum"><number>31</number></property></widget></item>
        <item row="6" column="0"><widget class="QLabel" name="labelPort"><property name="text"><string>\u7aef\u53e3\uff1a</string></property></widget></item>
        <item row="6" column="1"><widget class="QSpinBox" name="portSpinBox"><property name="minimum"><number>1</number></property><property name="maximum"><number>65535</number></property><property name="value"><number>102</number></property></widget></item>
        <item row="7" column="0"><widget class="QLabel" name="labelSlaveId"><property name="text"><string>\u4ece\u7ad9ID\uff1a</string></property></widget></item>
        <item row="7" column="1"><widget class="QSpinBox" name="slaveIdSpinBox"><property name="minimum"><number>1</number></property><property name="maximum"><number>247</number></property><property name="value"><number>1</number></property></widget></item>
        <item row="8" column="0"><widget class="QLabel" name="labelPollingInterval"><property name="text"><string>\u8f6e\u8be2\u95f4\u9694(ms)\uff1a</string></property></widget></item>
        <item row="8" column="1"><widget class="QSpinBox" name="pollingIntervalSpinBox"><property name="minimum"><number>100</number></property><property name="maximum"><number>10000</number></property><property name="value"><number>1000</number></property></widget></item>
       </layout>
      </widget>
      <widget class="QWidget" name="groupPropertyPage">
       <layout class="QFormLayout" name="groupForm">
        <item row="0" column="0"><widget class="QLabel" name="labelGroupName"><property name="text"><string>\u5206\u7ec4\u540d\u79f0\uff1a</string></property></widget></item>
        <item row="0" column="1"><widget class="QLineEdit" name="groupNameEdit"/></item>
        <item row="1" column="0"><widget class="QLabel" name="labelGroupPath"><property name="text"><string>\u6240\u5728\u8def\u5f84\uff1a</string></property></widget></item>
        <item row="1" column="1"><widget class="QLabel" name="groupPathLabel"><property name="text"><string>\u8bbe\u5907/\u4e0a\u7ea7\u5206\u7ec4</string></property></widget></item>
        <item row="2" column="0" colspan="2"><widget class="QLabel" name="labelGroupTags"><property name="text"><string>\u5305\u542b\u6807\u7b7e\uff1a</string></property></widget></item>
        <item row="3" column="0" colspan="2"><widget class="QListWidget" name="groupTagList"/></item>
       </layout>
      </widget>
      <widget class="QWidget" name="tagPropertyPage">
       <layout class="QFormLayout" name="tagForm">
        <item row="0" column="0"><widget class="QLabel" name="labelTagName"><property name="text"><string>\u6807\u7b7e\u540d\uff1a</string></property></widget></item>
        <item row="0" column="1"><widget class="QLineEdit" name="tagNameEdit"/></item>
        <item row="1" column="0"><widget class="QLabel" name="labelAddress"><property name="text"><string>\u5730\u5740\uff1a</string></property></widget></item>
        <item row="1" column="1"><widget class="QLineEdit" name="addressEdit"/></item>
        <item row="2" column="0"><widget class="QLabel" name="labelDataType"><property name="text"><string>\u6570\u636e\u7c7b\u578b\uff1a</string></property></widget></item>
        <item row="2" column="1"><widget class="QComboBox" name="dataTypeCombo"><item><property name="text"><string>BOOL</string></property></item><item><property name="text"><string>BYTE</string></property></item><item><property name="text"><string>WORD</string></property></item><item><property name="text"><string>DWORD</string></property></item><item><property name="text"><string>INT</string></property></item><item><property name="text"><string>DINT</string></property></item><item><property name="text"><string>REAL</string></property></item><item><property name="text"><string>STRING</string></property></item></widget></item>
        <item row="3" column="0"><widget class="QLabel" name="labelWritable"><property name="text"><string>\u53ef\u5199\uff1a</string></property></widget></item>
        <item row="3" column="1"><widget class="QCheckBox" name="writableCheckBox"/></item>
        <item row="4" column="0"><widget class="QLabel" name="labelDbEnabled"><property name="text"><string>\u8bb0\u5f55DB\uff1a</string></property></widget></item>
        <item row="4" column="1"><widget class="QCheckBox" name="dbEnabledCheckBox"><property name="checked"><bool>true</bool></property></widget></item>
        <item row="5" column="0"><widget class="QLabel" name="labelDescription"><property name="text"><string>\u63cf\u8ff0\uff1a</string></property></widget></item>
        <item row="5" column="1"><widget class="QLineEdit" name="descriptionEdit"/></item>
        <item row="6" column="0"><widget class="QLabel" name="labelScaling"><property name="text"><string>\u7f29\u653e\u7cfb\u6570\uff1a</string></property></widget></item>
        <item row="6" column="1"><widget class="QDoubleSpinBox" name="scalingSpinBox"><property name="decimals"><number>3</number></property><property name="value"><double>0.000000000000000</double></property></widget></item>
        <item row="7" column="0"><widget class="QLabel" name="labelOffset"><property name="text"><string>\u504f\u79fb\u91cf\uff1a</string></property></widget></item>
        <item row="7" column="1"><widget class="QDoubleSpinBox" name="offsetSpinBox"><property name="decimals"><number>3</number></property></widget></item>
        <item row="8" column="0"><widget class="QLabel" name="labelUnit"><property name="text"><string>\u5355\u4f4d\uff1a</string></property></widget></item>
        <item row="8" column="1"><widget class="QLineEdit" name="unitEdit"/></item>
        <item row="9" column="0" colspan="2">
         <widget class="QGroupBox" name="groupBox">
          <property name="title"><string>\u62a5\u8b66\u914d\u7f6e</string></property>
          <layout class="QGridLayout" name="gridLayout">
           <item row="0" column="0"><widget class="QLabel" name="label"><property name="text"><string>\u4e0b\u9650\u62a5\u8b66</string></property></widget></item>
           <item row="1" column="1"><widget class="QCheckBox" name="chkMaxAlarm"><property name="text"><string>\u542f\u7528</string></property></widget></item>
           <item row="2" column="1"><widget class="QComboBox" name="m_comboAlarmLevel"><item><property name="text"><string>\u4fe1\u606f</string></property></item><item><property name="text"><string>\u8b66\u544a</string></property></item><item><property name="text"><string>\u9519\u8bef</string></property></item><item><property name="text"><string>\u4e25\u91cd</string></property></item></widget></item>
           <item row="1" column="0"><widget class="QLabel" name="label_2"><property name="text"><string>\u4e0a\u9650\u62a5\u8b66</string></property></widget></item>
           <item row="2" column="0"><widget class="QLabel" name="label_3"><property name="text"><string>\u62a5\u8b66\u7ea7\u522b</string></property></widget></item>
           <item row="0" column="1"><widget class="QCheckBox" name="chkMinAlarm"><property name="text"><string>\u542f\u7528</string></property></widget></item>
           <item row="0" column="2"><widget class="QDoubleSpinBox" name="spinMinValue"/></item>
           <item row="1" column="2"><widget class="QDoubleSpinBox" name="spinMaxValue"/></item>
          </layout>
         </widget>
        </item>
       </layout>
      </widget>
     </widget>
    </widget>
   </item>
   <item>
    <layout class="QHBoxLayout" name="bottomButtonLayout">
     <item><spacer name="horizontalSpacer_2"><property name="orientation"><enum>Qt::Orientation::Horizontal</enum></property></spacer></item>
     <item><widget class="QPushButton" name="saveBtn"><property name="text"><string>\u4fdd\u5b58\u914d\u7f6e</string></property></widget></item>
     <item><spacer name="horizontalSpacer_4"><property name="orientation"><enum>Qt::Orientation::Horizontal</enum></property></spacer></item>
     <item><widget class="QPushButton" name="closeBtn"><property name="text"><string>\u5173\u95ed</string></property></widget></item>
     <item><spacer name="horizontalSpacer_3"><property name="orientation"><enum>Qt::Orientation::Horizontal</enum></property></spacer></item>
    </layout>
   </item>
  </layout>
 </widget>
 <resources/>
 <connections/>
</ui>'''

with open(r'D:\plcmonitor20260610\src\ui\dialogs\DeviceConfigDialog.ui', 'w', encoding='utf-8') as f:
    f.write(content)
print('OK')
