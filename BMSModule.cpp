#include "config.h"
#include "BMSModule.h"
#include "BMSUtil.h"
#include "Logger.h"


BMSModule::BMSModule()
{
  for (int i = 0; i < 12; i++)
  {
    cellVolt[i] = 0.0f;
    lowestCellVolt[i] = 5.0f;
    highestCellVolt[i] = 0.0f;
  }
  moduleVolt = 0.0f;
  temperatures[0] = 0.0f;
  temperatures[1] = 0.0f;
  temperatures[2] = 0.0f;
  temperatures[3] = 0.0f;
  lowestTemperature = 200.0f;
  highestTemperature = -100.0f;
  lowestModuleVolt = 200.0f;
  highestModuleVolt = 0.0f;
  exists = false;
  reset = false;
  moduleAddress = 0;
}

void BMSModule::clearmodule()
{
  for (int i = 0; i < 12; i++)
  {
    cellVolt[i] = 0.0f;
  }
  moduleVolt = 0.0f;
  temperatures[0] = 0.0f;
  temperatures[1] = 0.0f;
  temperatures[2] = 0.0f;
  temperatures[3] = 0.0f;
  exists = false;
  reset = false;
  moduleAddress = 0;
}

void BMSModule::decodetemp(CAN_message_t &msg)
{
  for (int g = 0; g < 4; g++)
  {
    temperatures[g] = msg.buf[g] - 40;
  }
}

void BMSModule::decodecan(int Id, CAN_message_t &msg)
{
  switch (Id)
  {
    case 1:
      cellVolt[0] = float(msg.buf[0] + (msg.buf[1] & 0x2F) * 256) / 1000;
      cellVolt[1] = float(msg.buf[2] + (msg.buf[3] & 0x2F) * 256) / 1000;
      cellVolt[2] = float(msg.buf[4] + (msg.buf[5] & 0x2F) * 256) / 1000;
      break;

    case 2:
      cellVolt[3] = float(msg.buf[0] + (msg.buf[1] & 0x2F) * 256) / 1000;
      cellVolt[4] = float(msg.buf[2] + (msg.buf[3] & 0x2F) * 256) / 1000;
      cellVolt[5] = float(msg.buf[4] + (msg.buf[5] & 0x2F) * 256) / 1000;
      break;

    case 3:
      cellVolt[6] = float(msg.buf[0] + (msg.buf[1] & 0x2F) * 256) / 1000;
      cellVolt[7] = float(msg.buf[2] + (msg.buf[3] & 0x2F) * 256) / 1000;
      cellVolt[8] = float(msg.buf[4] + (msg.buf[5] & 0x2F) * 256) / 1000;
      break;

    case 4:
      cellVolt[9] = float(msg.buf[0] + (msg.buf[1] & 0x2F) * 256) / 1000;
      cellVolt[10] = float(msg.buf[2] + (msg.buf[3] & 0x2F) * 256) / 1000;
      cellVolt[11] = float(msg.buf[4] + (msg.buf[5] & 0x2F) * 256) / 1000;
      break;

    default:

      break;
  }
  for (int i = 0; i < 12; i++)
  {
    if (lowestCellVolt[i] > cellVolt[i] && cellVolt[i] >= IgnoreCell) lowestCellVolt[i] = cellVolt[i];
    if (highestCellVolt[i] < cellVolt[i] && cellVolt[i] > 5.0) highestCellVolt[i] = cellVolt[i];
  }
}


/*
  Reading the status of the board to identify any flags, will be more useful when implementing a sleep cycle
*/

uint8_t BMSModule::getFaults()
{
  return faults;
}

uint8_t BMSModule::getAlerts()
{
  return alerts;
}

uint8_t BMSModule::getCOVCells()
{
  return COVFaults;
}

uint8_t BMSModule::getCUVCells()
{
  return CUVFaults;
}

/*
  Reading the setpoints, after a reset the default tesla setpoints are loaded
  Default response : 0x10, 0x80, 0x31, 0x81, 0x08, 0x81, 0x66, 0xff
*/
/*
  void BMSModule::readSetpoint()
  {
  uint8_t payload[3];
  uint8_t buff[12];
  payload[0] = moduleAddress << 1; //adresss
  payload[1] = 0x40;//Alert Status start
  payload[2] = 0x08;//two registers
  sendData(payload, 3, false);
  delay(2);
  getReply(buff);

  OVolt = 2.0+ (0.05* buff[5]);
  UVolt = 0.7 + (0.1* buff[7]);
  Tset = 35 + (5 * (buff[9] >> 4));
  } */

bool BMSModule::readModuleValues()
{
  uint8_t payload[4];
  uint8_t buff[50];
  uint8_t calcCRC;
  bool retVal = false;
  int retLen;
  float tempCalc;
  float tempTemp;

  payload[0] = moduleAddress << 1;

  readStatus();
  Logger::debug("Module %i   alerts=%X   faults=%X   COV=%X   CUV=%X", moduleAddress, alerts, faults, COVFaults, CUVFaults);

  payload[1] = REG_ADC_CTRL;
  payload[2] = 0b00111101; //ADC Auto mode, read every ADC input we can (Both Temps, Pack, 6 cells)
  BMSUtil::sendDataWithReply(payload, 3, true, buff, 3);

  payload[1] = REG_IO_CTRL;
  payload[2] = 0b00000011; //enable temperature measurement VSS pins
  BMSUtil::sendDataWithReply(payload, 3, true, buff, 3);

  payload[1] = REG_ADC_CONV; //start all ADC conversions
  payload[2] = 1;
  BMSUtil::sendDataWithReply(payload, 3, true, buff, 3);

  payload[1] = REG_GPAI; //start reading registers at the module voltage registers
  payload[2] = 0x12; //read 18 bytes (Each value takes 2 - ModuleV, CellV1-6, Temp1, Temp2)
  retLen = BMSUtil::sendDataWithReply(payload, 3, false, buff, 22);

  calcCRC = BMSUtil::genCRC(buff, retLen - 1);
  Logger::debug("Sent CRC: %x     Calculated CRC: %x", buff[21], calcCRC);

  //18 data bytes, address, command, length, and CRC = 22 bytes returned
  //Also validate CRC to ensure we didn't get garbage data.
  if ( (retLen == 22) && (buff[21] == calcCRC) )
  {
    if (buff[0] == (moduleAddress << 1) && buff[1] == REG_GPAI && buff[2] == 0x12) //Also ensure this is actually the reply to our intended query
    {
      //payload is 2 bytes gpai, 2 bytes for each of 6 cell voltages, 2 bytes for each of two temperatures (18 bytes of data)
      moduleVolt = (buff[3] * 256 + buff[4]) * 0.002034609f;
      if (moduleVolt > highestModuleVolt) highestModuleVolt = moduleVolt;
      if (moduleVolt < lowestModuleVolt) lowestModuleVolt = moduleVolt;
      for (int i = 0; i < 12; i++)
      {
        cellVolt[i] = (buff[5 + (i * 2)] * 256 + buff[6 + (i * 2)]) * 0.000381493f;
        if (lowestCellVolt[i] > cellVolt[i] && cellVolt[i] >= IgnoreCell) lowestCellVolt[i] = cellVolt[i];
        if (highestCellVolt[i] < cellVolt[i]) highestCellVolt[i] = cellVolt[i];
      }

      //Now using steinhart/hart equation for temperatures. We'll see if it is better than old code.
      tempTemp = (1.78f / ((buff[17] * 256 + buff[18] + 2) / 33046.0f) - 3.57f);
      tempTemp *= 1000.0f;
      tempCalc =  1.0f / (0.0007610373573f + (0.0002728524832 * logf(tempTemp)) + (powf(logf(tempTemp), 3) * 0.0000001022822735f));

      temperatures[0] = tempCalc - 273.15f;

      tempTemp = 1.78f / ((buff[19] * 256 + buff[20] + 9) / 33068.0f) - 3.57f;
      tempTemp *= 1000.0f;
      tempCalc = 1.0f / (0.0007610373573f + (0.0002728524832 * logf(tempTemp)) + (powf(logf(tempTemp), 3) * 0.0000001022822735f));
      temperatures[1] = tempCalc - 273.15f;

      if (getLowTemp() < lowestTemperature) lowestTemperature = getLowTemp();
      if (getHighTemp() > highestTemperature) highestTemperature = getHighTemp();

      Logger::debug("Got voltage and temperature readings");
      retVal = true;
    }
  }
  else
  {
    Logger::error("Invalid module response received for module %i  len: %i   crc: %i   calc: %i",
                  moduleAddress, retLen, buff[21], calcCRC);
  }

  //turning the temperature wires off here seems to cause weird temperature glitches
  // payload[1] = REG_IO_CTRL;
  // payload[2] = 0b00000000; //turn off temperature measurement pins
  // BMSUtil::sendData(payload, 3, true);
  // delay(3);
  // BMSUtil::getReply(buff, 50);    //TODO: we're not validating the reply here. Perhaps check to see if a valid reply came back

  return retVal;
}

float BMSModule::getCellVoltage(int cell)
{
  if (cell < 0 || cell > 12) return 0.0f;
  return cellVolt[cell];
}

float BMSModule::getLowCellV()
{
  float lowVal = 10.0f;
  for (int i = 0; i < 12; i++) if (cellVolt[i] < lowVal && cellVolt[i] > IgnoreCell) lowVal = cellVolt[i];
  return lowVal;
}

float BMSModule::getHighCellV()
{
  float hiVal = 0.0f;
  for (int i = 0; i < 12; i++)
    if (cellVolt[i] > IgnoreCell && cellVolt[i] < 5.0)
    {
      if (cellVolt[i] > hiVal) hiVal = cellVolt[i];
    }
  return hiVal;
}

float BMSModule::getAverageV()
{
  int x = 0;
  float avgVal = 0.0f;
  for (int i = 0; i < 12; i++)
  {
    if (cellVolt[i] > IgnoreCell && cellVolt[i] < 5.0)
    {
      x++;
      avgVal += cellVolt[i];
    }
  }

  scells = x;
  avgVal /= x;
  return avgVal;
}

int BMSModule::getscells()
{
  return scells;
}

float BMSModule::getHighestModuleVolt()
{
  return highestModuleVolt;
}

float BMSModule::getLowestModuleVolt()
{
  return lowestModuleVolt;
}

float BMSModule::getHighestCellVolt(int cell)
{
  if (cell < 0 || cell > 12) return 0.0f;
  return highestCellVolt[cell];
}

float BMSModule::getLowestCellVolt(int cell)
{
  if (cell < 0 || cell > 12) return 0.0f;
  return lowestCellVolt[cell];
}

float BMSModule::getHighestTemp()
{
  return highestTemperature;
}

float BMSModule::getLowestTemp()
{
  return lowestTemperature;
}

float BMSModule::getLowTemp()
{
  if (temperatures[0] < temperatures[1] && temperatures[0] < temperatures[2] && temperatures[0] < temperatures[3])
  {
    return (temperatures[0]);
  }
    if (temperatures[1] < temperatures[0] && temperatures[1] < temperatures[2] && temperatures[1] < temperatures[3])
  {
    return (temperatures[1]);
  }
    if (temperatures[2] < temperatures[0] && temperatures[2] < temperatures[1] && temperatures[2] < temperatures[3])
  {
    return (temperatures[2]);
  }
    if (temperatures[3] < temperatures[1] && temperatures[3] < temperatures[2] && temperatures[3] < temperatures[0])
  {
    return (temperatures[3]);
  }
}

float BMSModule::getHighTemp()
{
    if (temperatures[0] > temperatures[1] && temperatures[0] > temperatures[2] && temperatures[0] > temperatures[3])
  {
    return (temperatures[0]);
  }
    if (temperatures[1] > temperatures[0] && temperatures[1] > temperatures[2] && temperatures[1] > temperatures[3])
  {
    return (temperatures[1]);
  }
    if (temperatures[2] > temperatures[0] && temperatures[2] > temperatures[1] && temperatures[2] > temperatures[3])
  {
    return (temperatures[2]);
  }
    if (temperatures[3] > temperatures[1] && temperatures[3] > temperatures[2] && temperatures[3] > temperatures[0])
  {
    return (temperatures[3]);
  }
}

float BMSModule::getAvgTemp()
{
  float avgtemp = 0;

  avgtemp = temperatures[0] + temperatures[1] + temperatures[2] + temperatures[3];
  avgtemp = avgtemp * 0.25;
  return (avgtemp);
}

float BMSModule::getModuleVoltage()
{
  moduleVolt = 0;
  for (int I; I < 12; I++)
  {
    if (cellVolt[I] > IgnoreCell && cellVolt[I] < 5.0)
    {
      moduleVolt = moduleVolt + cellVolt[I];
    }
  }
  return moduleVolt;
}

float BMSModule::getTemperature(int temp)
{
  if (temp < 0 || temp > 2) return 0.0f;
  return temperatures[temp];
}

void BMSModule::setAddress(int newAddr)
{
  if (newAddr < 0 || newAddr > MAX_MODULE_ADDR) return;
  moduleAddress = newAddr;
}

int BMSModule::getAddress()
{
  return moduleAddress;
}

bool BMSModule::isExisting()
{
  return exists;
}

bool BMSModule::isReset()
{
  return reset;
}


void BMSModule::settempsensor(int tempsensor)
{
  sensor = tempsensor;
}

void BMSModule::setExists(bool ex)
{
  exists = ex;
}

void BMSModule::setReset(bool ex)
{
  reset = ex;
}

void BMSModule::setIgnoreCell(float Ignore)
{
  IgnoreCell = Ignore;
  Serial.println();
  Serial.println();
  Serial.println(Ignore);
  Serial.println();

}
