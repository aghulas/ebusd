/*
 * ebusd - daemon for communication with eBUS heating systems.
 * Copyright (C) 2014-2026 John Baier <ebusd@ebusd.eu>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <unistd.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>
#include <map>
#include "lib/ebus/message.h"

using namespace ebusd;
using std::cout;
using std::endl;

static bool error = false;

void verify(bool expectFailMatch, string type, string input,
    bool match, string expectStr, string gotStr) {
  if (expectFailMatch) {
    if (match) {
      error = true;
      cout << "  failed " << type << " match >" << input
              << "< error: unexpectedly succeeded" << endl;
    } else {
      cout << "  failed " << type << " match >" << input << "< OK" << endl;
    }
  } else if (match) {
    cout << "  " << type << " match >" << input << "< OK" << endl;
  } else {
    error = true;
    cout << "  " << type << " match >" << input << "< error: got >"
            << gotStr << "<, expected >" << expectStr << "<" << endl;
  }
}

DataFieldTemplates* templates = nullptr;

/** Test messages with IDs longer than 7 bytes (the ID length in the message key is limited to 7). */
void testLongId(MessageMap* messages) {
  cout << "long ID:" << endl;
  messages->clear();
  istringstream stream(
    "type,circuit,level,name,comment,qq,zz,pbsb,id,*name,part,type,divisor/values,unit,comment\n"
    "r,c,,Short,,,15,5022,01,,,UCH,,,\n"
    "r,c,,Seven,,,15,5022,01020304050607,,,UCH,,,\n"
    "r,c,,Nine,,,15,5022,010203040506070809,,,UCH,,,\n"
    "r,c,,NineB,,,15,5022,01020304050607080a,,,UCH,,,\n"
    "r,c,,Seventeen,,,15,5022,0102030405060708090a0b0c0d0e0f1011,,,UCH,,,\n"
    "r,c,,NineZero,,,15,5022,010000000000000000,,,UCH,,,\n");  // same folded key bytes as "Short"
  map<string, string> defaults;
  string err;
  result_t result = messages->readFromStream(&stream, "test.csv", 0, false, &defaults, &err);
  if (result != RESULT_OK) {
    cout << "  load FAILED: " << getResultCode(result) << " " << err << endl;
    error = true;
    return;
  }
  const char* checks[][2] = {
    {"31155022""09""010203040506070809", "Nine"},
    {"31155022""09""01020304050607080a", "NineB"},
    {"31155022""09""01020304050607080b", "Seven"},
    {"31155022""07""01020304050607", "Seven"},
    {"31155022""11""0102030405060708090a0b0c0d0e0f1011", "Seventeen"},
    {"31155022""09""01ffffffffffffff09", "Short"},
    {"31155022""09""010000000000000000", "NineZero"},
  };
  for (const auto& check : checks) {
    MasterSymbolString master;
    master.parseHex(check[0]);
    Message* message = messages->find(master);
    string name = message ? message->getName() : "none";
    if (name == check[1]) {
      cout << "  find " << check[0] << " -> " << name << " OK" << endl;
    } else {
      cout << "  find " << check[0] << " -> " << name << " FAILED, expected " << check[1] << endl;
      error = true;
    }
  }
}


/**
 * Load message definitions from a CSV string with header line.
 * @param messages the @a MessageMap to load into (cleared before).
 * @param csv the CSV with header line.
 * @param errorDescription the error description.
 * @return the result code.
 */
result_t loadFillsCsv(MessageMap* messages, const string& csv, string* errorDescription) {
  messages->clear();
  istringstream stream(csv);
  map<string, string> defaults;
  return messages->readFromStream(&stream, "test.csv", 0, false, &defaults, errorDescription);
}

void checkFills(bool ok, const string& what) {
  cout << "  " << what << (ok ? " OK" : " FAILED") << endl;
  if (!ok) {
    error = true;
  }
}

/** Test the carrier messages filling other messages (field attribute "fills"). */
void testFills(MessageMap* messages) {
  cout << "fills:" << endl;
  const string header =
      "type,circuit,level,name,comment,qq,zz,pbsb,id,*name,part,type,divisor/values,unit,comment,fills\n";
  const string targets =
      "r,fs1,,HolidayStartDay,,,35,5022,0d1b01,,,UIN,,,,\n"
      "r,fs1,,HolidayStartMonth,,,35,5022,c51c01,,,UIN,,,,\n"
      "r,fs1,,HolidayStartThird,,,35,5022,991d01,,,UIN,,,,\n";
  string err;
  // load errors
  const char* badCarriers[] = {
    "r,fs1,,C,,,35,5022,d91b011c011d01,a,,UIN,,,,HolidayStartDay,b,,STR:*,,,,HolidayStartMonth\n",  // variable
    "r,fs1,,C,,,35,5022,d91b011c011d01,a,,STR:*,,,,,b,,UIN,,,,HolidayStartMonth\n",  // after variable
    "r,fs1,,C,,,35,5022,d91b011c011d01,a,,BI0,,,,HolidayStartDay,b,,BI1,,,,\n",  // partial byte
    "r,fs1,,C,,,35,5022,d91b011c011d01,a,m,UCH,,,,HolidayStartDay\n",  // master data
    "w,fs1,,C,,,35,5023,d91b011c011d01,a,s,UIN,,,,HolidayStartDay\n",  // write
    "r,fs1,,C,,,35,5022,d91b011c011d01,a,,UCH,,,,HolidayStartDay,b,,UCH,,,,,c,,UCH,,,,HolidayStartDay\n",  // gap
  };
  for (const auto carrier : badCarriers) {
    err.clear();
    checkFills(loadFillsCsv(messages, header + targets + carrier, &err) != RESULT_OK, string("reject ") + carrier
        + " (" + err + ")");
  }
  // resolution errors
  const char* badTargets[][2] = {
    {"r,fs1,,C,,,35,5022,d91b011c011d01,a,,UIN,,,,Missing\n", "not found"},
    {"w,fs1,,T,,,35,5023,001b01,,,UIN,,,,\nr,fs1,,C,,,35,5022,d91b011c011d01,a,,UIN,,,,T\n", "not found"},
    {"r,fs1,,T,,,35,5022,001b01,x,m,UCH,,,,,v,s,UIN,,,,\nr,fs1,,C,,,35,5022,d91b011c011d01,a,,UIN,,,,T\n",
        "has master data"},
    {"r,fs1,,T,,,35,5022,001b01,,,UCH,,,,\nr,fs1,,C,,,35,5022,d91b011c011d01,a,,UIN,,,,T\n", "has another length"},
    {"r,fs1,,T,,,75,5022,001b01,,,UIN,,,,\nr,fs1,,C,,,35,5022,d91b011c011d01,a,,UIN,,,,T\n",
        "has another destination"},
    {"r,fs1,,T,,,35,5022,001b01,a,,UIN,,,,HolidayStartDay\nr,fs1,,C,,,35,5022,d91b011c011d01,a,,UIN,,,,T\n",
        "is a carrier"},
  };
  for (const auto& bad : badTargets) {
    err.clear();
    result_t result = loadFillsCsv(messages, header + targets + bad[0], &err);
    string resolveError;
    if (result == RESULT_OK) {
      result = messages->resolveFills(&resolveError);
    }
    checkFills(result != RESULT_OK && resolveError.find(bad[1]) != string::npos, "resolve error " + resolveError);
  }

  // distribution: a holiday date read with one telegram fills the three single messages
  const string carrier =
      "r,fs1,,HolidayStart,,,35,5022,d91b011c011d01,day,,UIN,,,,HolidayStartDay,month,,UIN,,,,HolidayStartMonth,"
      "third,,UIN,,,,HolidayStartThird\n";
  err.clear();
  result_t result = loadFillsCsv(messages, header + carrier + targets, &err);  // carrier before its targets
  checkFills(result == RESULT_OK, "load carrier " + err);
  if (result != RESULT_OK) {
    return;
  }
  Message* message = messages->find("fs1", "HolidayStart", "", false);
  checkFills(message && message->isCarrier() && message->getFillLinks().size() == 3, "carrier links");
  if (!message) {
    return;
  }
  MasterSymbolString master;
  SlaveSymbolString slave;
  master.parseHex("3135502207d91b011c011d01");
  slave.parseHex("061e000a000000");
  checkFills(messages->find(master) == message, "find carrier");
  message->storeLastData(master, slave);
  vector<Message*> updated;
  err.clear();
  result = messages->distribute(message, &updated, &err);
  checkFills(result == RESULT_OK && updated.size() == 3, "distribute " + err);
  const char* expected[][3] = {
    {"HolidayStartDay", "30", "3135502203""0d1b01"},
    {"HolidayStartMonth", "10", "3135502203""c51c01"},
    {"HolidayStartThird", "0", "3135502203""991d01"},
  };
  for (const auto& exp : expected) {
    Message* target = messages->find("fs1", exp[0], "", false);
    if (!target) {
      checkFills(false, string("find ") + exp[0]);
      continue;
    }
    ostringstream output;
    result = target->decodeLastData(pt_any, false, nullptr, -1, OF_NONE, &output);
    checkFills(result == RESULT_OK && output.str() == exp[1] && target->getLastMasterData().getStr() == exp[2]
        && target->getLastUpdateTime() >= message->getLastUpdateTime()
        && target->getLastUpdateTime() <= message->getLastUpdateTime() + 1,
        string("filled ") + exp[0] + " = " + output.str() + " from " + target->getLastMasterData().getStr());
    // a target is the one found for its own telegram, the carrier for the grouped one
    checkFills(messages->find(target->getLastMasterData()) == target, string("find ") + exp[0]);
    checkFills(messages->findFillCarrier(target) == message, string("carrier of ") + exp[0]);
  }
  // a short answer fills nothing
  SlaveSymbolString shortSlave;
  shortSlave.parseHex("041f000b00");
  message->storeLastData(master, shortSlave);
  updated.clear();
  err.clear();
  checkFills(messages->distribute(message, &updated, &err) != RESULT_OK && updated.empty(), "short answer");
  Message* day = messages->find("fs1", "HolidayStartDay", "", false);
  ostringstream output;
  day->decodeLastData(pt_any, false, nullptr, -1, OF_NONE, &output);
  checkFills(output.str() == "30", "short answer keeps value " + output.str());
  // a failed carrier is not used for polling
  message->setFillFailed(true);
  checkFills(messages->findFillCarrier(day) == nullptr, "failed carrier not used");
  message->setFillFailed(false);
  // a non-carrier is not distributed
  updated.clear();
  checkFills(messages->distribute(day, &updated, &err) == RESULT_EMPTY, "not a carrier");

  // a carrier without destination only fills a target of the destination of the telegram
  err.clear();
  result = loadFillsCsv(messages, header +
      "r,fs1,,AnyStart,,,,5022,d91b011c011d01,day,,UIN,,,,HolidayStartDay,month,,UIN,,,,Generic,third,,UIN,,,,\n"
      "r,fs1,,HolidayStartDay,,,35,5022,0d1b01,,,UIN,,,,\n"
      "r,fs1,,Generic,,,,5022,c51c01,,,UIN,,,,\n", &err);
  checkFills(result == RESULT_OK, "load carrier without destination " + err);
  message = messages->find("fs1", "AnyStart", "", false);
  Message* specific = messages->find("fs1", "HolidayStartDay", "", false);
  Message* generic = messages->find("fs1", "Generic", "", false);
  if (message && specific && generic) {
    MasterSymbolString other;
    SlaveSymbolString otherSlave;
    other.parseHex("3108502207d91b011c011d01");
    otherSlave.parseHex("06010002000300");
    message->storeLastData(other, otherSlave);
    updated.clear();
    err.clear();
    messages->distribute(message, &updated, &err);
    checkFills(updated.size() == 1 && updated[0] == generic && specific->getLastUpdateTime() == 0,
        "telegram to another destination fills only the target without destination");
  }

  // a carrier that cannot be sent on its own is not used for polling
  const char* unsendable[] = {
    "r,fs1,,C,,,,5022,d91b011c011d01,day,,UIN,,,,HolidayStartDay\n",  // no destination
    "r,fs1,,C,,,35,5022,d91b011c011d01,x,m,UCH,,,,,day,s,UIN,,,,HolidayStartDay\n",  // needs input
  };
  for (const auto def : unsendable) {
    err.clear();
    result = loadFillsCsv(messages, header + def + targets, &err);
    Message* target = messages->find("fs1", "HolidayStartDay", "", false);
    checkFills(result == RESULT_OK && target && messages->findFillCarrier(target) == nullptr,
        string("not polled through ") + def + err);
  }
  // a failed carrier stays failed when other messages are added (it is retried once answered)
  err.clear();
  result = loadFillsCsv(messages, header + carrier + targets, &err);
  message = messages->find("fs1", "HolidayStart", "", false);
  day = messages->find("fs1", "HolidayStartDay", "", false);
  if (message && day) {
    checkFills(messages->findFillCarrier(day) == message, "polled through carrier");
    message->setFillFailed(true);
    istringstream more("r,fs1,,Other,,,35,5022,001e01,,,UIN,,,,\n");
    map<string, string> defaults;
    messages->readFromStream(&more, "more.csv", 0, false, &defaults, &err);
    messages->resolveFills(&err);  // as after a scan
    checkFills(messages->findFillCarrier(day) == nullptr, "failed carrier stays failed after an addition");
  }

  // removing a target leaves no dangling link: invalidating after a write and distributing use the current messages
  err.clear();
  result = loadFillsCsv(messages, header + carrier + targets +
      "w,fs1,,HolidayStartDay,,,35,5023,0d1b01,,,UIN,,,,\n", &err);
  checkFills(result == RESULT_OK, "load carrier with write " + err);
  message = messages->find("fs1", "HolidayStart", "", false);
  Message* write = messages->find("fs1", "HolidayStartDay", "", true);
  Message* removed = messages->find("fs1", "HolidayStartDay", "", false);
  if (message && write && removed) {
    err.clear();
    messages->resolveFills(&err);
    messages->remove(removed);  // deletes it
    message->storeLastData(master, slave);
    messages->invalidateCache(write);
    updated.clear();
    err.clear();
    result = messages->distribute(message, &updated, &err);
    checkFills(updated.size() == 2 && updated[0]->getName() == "HolidayStartMonth"
        && updated[1]->getName() == "HolidayStartThird", "removed target not filled");
  }

  // a memory carrier with a verified first byte only decodes with that byte 0
  err.clear();
  result = loadFillsCsv(messages, header +
      "r,bc,,Values,,,08,5000,e80601f2d8,firstbyte,,UCH,==0,,,,a,,UIN,,,,A,b,,HEX:14,,,,\n"
      "r,bc,,A,,,08,5000,000601f2d8,,,UIN,,,,\n", &err);
  checkFills(result == RESULT_OK, "load memory carrier " + err);
  message = messages->find("bc", "Values", "", false);
  if (message) {
    MasterSymbolString memMaster;
    SlaveSymbolString memSlave, memSlaveBad;
    memMaster.parseHex("3108500005e80601f2d8");
    memSlave.parseHex("11000102030405060708090a0b0c0d0e0f10");
    memSlaveBad.parseHex("11030102030405060708090a0b0c0d0e0f10");
    message->storeLastData(memMaster, memSlave);
    ostringstream out;
    checkFills(message->decodeLastData(pt_any, false, nullptr, -1, OF_NONE, &out) >= RESULT_OK,
        "memory carrier decodes with first byte 0");
    message->storeLastData(memMaster, memSlaveBad);
    ostringstream out2;
    checkFills(message->decodeLastData(pt_any, false, nullptr, -1, OF_NONE, &out2) < RESULT_OK,
        "memory carrier fails with first byte 3");
  }
}


namespace ebusd {

class TestResolver : public Resolver {
 public:
  virtual DataFieldTemplates* getTemplates(const string& filename) {
    if (filename == "") {  // avoid compiler warning
      return templates;
    }
    return templates;
  }

  virtual result_t loadDefinitionsFromConfigPath(FileReader* reader, const string& filename,
      map<string, string>* defaults, string* errorDescription, bool replace = false) {
    time_t mtime = 0;
    istream* stream = FileReader::openFile(filename, errorDescription, &mtime);
    result_t result;
    if (stream) {
      result = reader->readFromStream(stream, filename, mtime, false, defaults, errorDescription);
      delete(stream);
    } else {
      result = RESULT_ERR_NOTFOUND;
    }
    return result;
  }
};

}  // namespace ebusd


/**
 * Load message definitions from a CSV string with header line into the @a MessageMap.
 * @param messages the @a MessageMap to load into (cleared before).
 * @param csv the CSV with header line.
 * @param errorDescription the error description.
 * @return the result code.
 */
result_t loadCsv(MessageMap* messages, const string& csv, string* errorDescription) {
  messages->clear();
  istringstream stream(csv);
  map<string, string> defaults;
  return messages->readFromStream(&stream, "test.csv", 0, false, &defaults, errorDescription);
}

void check(bool ok, const string& what) {
  if (ok) {
    cout << "  " << what << " OK" << endl;
  } else {
    cout << "  " << what << " FAILED" << endl;
    error = true;
  }
}

/** Test the computed byte of the master data (attribute "crc"). */
void testComputedByte(MessageMap* messages) {
  cout << "computed byte:" << endl;
  // parser
  const char* goodDefs[] = {"crc8", "crc8;poly=5c", "CRC8; poly=5c; init=00; pos=0; from=1; to=*; check=strict",
    "crc8;pos=4;from=0;to=4", "crc8;check=ignore"};
  for (const auto def : goodDefs) {
    ComputedByte computed;
    string err;
    check(computed.parse(def, &err) == RESULT_OK && computed.isEnabled(), string("parse \"") + def + "\"");
  }
  const char* badDefs[] = {"", "crc16", "crc8;poly=xyz", "crc8;poly", "crc8;foo=1", "crc8;check=lax",
    "crc8;pos=1;from=0", "crc8;from=3;to=2", "crc8;pos=1;from=0;to=4"};
  for (const auto def : badDefs) {
    ComputedByte computed;
    string err;
    check(computed.parse(def, &err) != RESULT_OK && !computed.isEnabled(), string("reject \"") + def + "\"");
  }

  // algorithm: CRC-8 with polynomial 0x5c over known telegrams, the byte placeholder being replaced
  const char* vectors[][2] = {
    {"0015502203002500", "4c"},  // register read
    {"00155023050025000200", "cc"},  // register write
    {"0035502207001b011c011d01", "d9"},  // read of three registers
    {"0008500005000601f2d8", "e8"},  // memory read
    {"00085023090042010400" "5d010000", "d8"},  // Wolf HG07 write (john30/ebusd#167)
  };
  ComputedByte wh;
  string err;
  wh.parse("crc8;poly=5c", &err);
  for (const auto vec : vectors) {
    MasterSymbolString master;
    master.parseHex(vec[0]);
    symbol_t value = 0;
    MasterSymbolString expect;
    expect.parseHex(vec[1]);
    check(wh.compute(master, &value) == RESULT_OK && value == expect[0], string("compute ") + vec[0]);
  }
  // default polynomial: the eBUS frame CRC (SymbolString::calcCrc), here over a whole telegram without escapes
  ComputedByte frame;
  frame.parse("crc8;pos=8;from=0;to=8", &err);
  MasterSymbolString frameData, frameTelegram;
  frameData.parseHex("0000000009""3108b509030d2800""00");
  frameTelegram.parseHex("3108b509030d2800");
  symbol_t frameValue = 0;
  check(frame.compute(frameData, &frameValue) == RESULT_OK && frameValue == frameTelegram.calcCrc(),
      "compute with eBUS polynomial");
  MasterSymbolString shortData;
  shortData.parseHex("0000000003""3108b5");
  check(frame.compute(shortData, &frameValue) == RESULT_ERR_INVALID_POS, "compute beyond data rejected");

  // definitions: errors
  const string header =
      "type,circuit,level,name,comment,qq,zz,pbsb,id,crc,*name,part,type,divisor/values,unit,comment\n";
  check(loadCsv(messages, header + "r,ka,,Bad,,,15,5022,4c2500,crc8;poly=zz,,,UIN,,,\n", &err) != RESULT_OK,
      "reject bad crc attribute");
  TestResolver checkingResolver;  // declared first to outlive the map using it
  MessageMap checkingMap(false, "", false);  // not adding all, i.e. with duplicate check
  checkingMap.setResolver(&checkingResolver);
  check(loadCsv(&checkingMap, header + "r,ka,,A,,,15,5022,4c2500,crc8;poly=5c,,,UIN,,,\n"
      "r,ka,,B,,,15,5022,002500,crc8;poly=5c,,,UIN,,,\n", &err) == RESULT_ERR_DUPLICATE,
      "reject duplicate differing only in the computed byte " + err);
  check(loadCsv(&checkingMap, header + "r,ka,,A,,,15,5022,4c2500,crc8;poly=5c,,,UIN,,,\n"
      "r,ka,,B,,,15,5022,002500,,,,UIN,,,\n", &err) == RESULT_OK,
      "accept same ID with and without computed byte");
  // duplicates, the same in both orders and whatever placeholder is written in the ID: a message with a computed
  // byte is never a duplicate of one without (the latter is found for its exact ID), two with a computed byte are
  // compared without it
  const char* duplicates[][3] = {
    // first, second, "1" if duplicate
    {"r,ka,,A,,,15,5022,002500,,,,UIN,,,", "r,ka,,B,,,15,5022,002500,crc8;poly=5c,,,UIN,,,", "0"},
    {"r,ka,,A,,,15,5022,4c2500,,,,UIN,,,", "r,ka,,B,,,15,5022,002500,crc8;poly=5c,,,UIN,,,", "0"},
    {"r,ka,,A,,,15,5022,4c2500,,,,UIN,,,", "r,ka,,B,,,15,5022,4c2500,crc8;poly=5c,,,UIN,,,", "0"},
    {"r,ka,,A,,,15,5022,002500,crc8;poly=5c,,,UIN,,,", "r,ka,,B,,,15,5022,4c2500,crc8;poly=5c,,,UIN,,,", "1"},
    {"r,ka,,A,,,15,5022,002500,crc8;poly=5c,,,UIN,,,", "r,ka,,B,,,15,5022,002600,crc8;poly=5c,,,UIN,,,", "0"},
  };
  for (const auto& dup : duplicates) {
    for (int order = 0; order < 2; order++) {
      string first = dup[order], second = dup[1-order];
      result_t res = loadCsv(&checkingMap, header + first + "\n" + second + "\n", &err);
      bool isDuplicate = res == RESULT_ERR_DUPLICATE;
      check(isDuplicate == (dup[2][0] == '1') && (isDuplicate || res == RESULT_OK),
          string(isDuplicate ? "duplicate " : "no duplicate ") + first + " / " + second);
    }
  }

  // definitions: send and receive
  string csv =
    "type,circuit,level,name,comment,qq,zz,pbsb,id,crc,*name,part,type,divisor/values,unit,comment\n"
    "r,ka,,BlockingTime,,,15,5022,4c2500,crc8;poly=5c,,,UIN,,,\n"
    "w,ka,,BlockingTime,,,15,5023,4c2500,crc8;poly=5c,,,UIN,,,\n"
    "r,ka,,Other,,,15,5022,ab2600,crc8;poly=5c,,,UIN,,,\n"
    "r,ka,,Plain,,,15,5022,002700,,,,UIN,,,\n"
    "w,bm,,HG07,,,08,5023,c14201,crc8;poly=5c,,,UIN,,min,,suffix,,HEX:4,=5d010000,,\n"
    "r,bm,,Lax,,,08,5022,cc2d00,crc8;poly=5c;check=ignore,,,UIN,,,\n"
    "w,bm,,Trailer,,,08,b509,0d,crc8;pos=3;from=0;to=3,,,UIN,,,,crc,,HEX:1,,,\n";
  result_t result = loadCsv(messages, csv, &err);
  check(result == RESULT_OK, "load definitions " + err);
  if (result != RESULT_OK) {
    return;
  }
  struct {
    const char* circuit;
    const char* name;
    bool isWrite;
    const char* input;
    const char* expect;
  } sends[] = {
    {"ka", "BlockingTime", false, "", "3115502203""4c2500"},
    // writes of BlockingTime (register 0x0025) as taken on the bus by a Weishaupt system manager
    {"ka", "BlockingTime", true, "2", "3115502305""cc25000200"},
    {"ka", "BlockingTime", true, "1", "3115502305""2825000100"},
    {"bm", "HG07", true, "4;5d010000", "3108502309""d842010400" "5d010000"},
    {"ka", "Plain", false, "", "3115502203""002700"},
    {"bm", "Trailer", true, "2", "3108b50904""0d0200" "53"},
  };
  for (const auto& send : sends) {
    Message* message = messages->find(send.circuit, send.name, "", send.isWrite);
    if (!message) {
      check(false, string("find ") + send.name);
      continue;
    }
    istringstream input(send.input);
    MasterSymbolString master;
    result = message->prepareMaster(0, 0x31, SYN, UI_FIELD_SEPARATOR, &input, &master);
    MasterSymbolString expect;
    expect.parseHex(send.expect);
    check(result == RESULT_OK && master == expect,
        string("prepare ") + send.name + " " + send.input + ": " + master.getStr() + " expected " + expect.getStr());
  }
  // aliases: "[WCRC]" at the start of the comment and attribute "wcrc" behave like "crc8;poly=5c"
  {
    string err2;
    const string aliasHeader =
        "type,circuit,level,name,comment,qq,zz,pbsb,id,wcrc,*name,part,type,divisor/values,unit,comment\n";
    check(loadCsv(messages, aliasHeader +
        "w,ka,,ByComment,[WCRC] setting,,15,5023,4c2500,,,,UIN,,,\n"
        "w,ka,,ByAttribute,,,15,5023,ab2600,1,,,UIN,,,\n"
        "w,ka,,NotAtStart,setting [WCRC],,15,5023,4c2700,,,,UIN,,,\n", &err2) == RESULT_OK, "load aliases " + err2);
    const char* names[][2] = {{"ByComment", "1"}, {"ByAttribute", "1"}, {"NotAtStart", "0"}};
    for (const auto& name : names) {
      Message* message = messages->find("ka", name[0], "", true);
      check(message != nullptr && message->getComputedByte().isEnabled() == (name[1][0] == '1'),
          string("alias ") + name[0]);
    }
    Message* message = messages->find("ka", "ByComment", "", true);
    if (message) {
      istringstream input("2");
      MasterSymbolString master;
      check(message->prepareMaster(0, 0xff, SYN, UI_FIELD_SEPARATOR, &input, &master) == RESULT_OK
          && master.getStr() == "ff15502305cc25000200", "alias prepare " + master.getStr());
    }
    // the alias is checked like the attribute, e.g. not in the varying part of chained IDs
    check(loadCsv(messages, aliasHeader + "r,ka,,Chained,[WCRC],,15,5022,1b01:2;1c01:2,,a,,UIN,,,,b,,UIN,,,\n", &err2)
        != RESULT_OK, "reject alias in the varying part of chained IDs: " + err2);
  }
  // chained message with the computed byte in the common prefix of its IDs
  {
    string err2;
    check(loadCsv(messages, header +
        "r,ka,,Chained,,,15,5022,000d01:2;000d02:2,crc8;poly=5c,a,,UIN,,,,b,,UIN,,,\n", &err2) == RESULT_OK,
        "load chained " + err2);
    Message* message = messages->find("ka", "Chained", "", false);
    check(message && message->getCount() == 2, "chained parts");
    const char* parts[] = {"3115502203""290d01", "3115502203""2a0d02"};
    for (size_t index = 0; message && index < 2; index++) {
      istringstream input("");
      MasterSymbolString master;
      result = message->prepareMaster(index, 0x31, SYN, UI_FIELD_SEPARATOR, &input, &master);
      check(result == RESULT_OK && master.getStr() == parts[index], string("chained prepare ") + master.getStr());
      size_t foundIndex = 99;
      check(messages->find(master) == message && message->checkId(master, &foundIndex) && foundIndex == index,
          string("chained find ") + master.getStr());
      master[5] ^= 1;
      check(messages->find(master) != message, string("chained wrong CRC ") + master.getStr());
    }
    check(loadCsv(messages, header +
        "r,ka,,Chained,,,15,5022,000d01:2;000d02:2,crc8;poly=5c;pos=2;from=0;to=2,a,,UIN,,,,b,,UIN,,,\n", &err2)
        != RESULT_OK, "reject computed byte in the varying part of chained IDs: " + err2);
  }
  // precedence: a message with a computed byte in a longer ID wins over a shorter ID matching the same telegram
  {
    string err2;
    check(loadCsv(messages, header +
        "r,ka,,Generic,,,15,5022,,,,,UCH,,,\n"
        "r,ka,,Short,,,15,5022,4c,,,,UCH,,,\n"
        "r,ka,,Reg,,,15,5022,4c2500,crc8;poly=5c,,,UIN,,,\n"
        "r,ka,,Reg26,,,15,5022,002600,crc8;poly=5c,,,UIN,,,\n", &err2) == RESULT_OK, "load precedence " + err2);
    const char* precedence[][2] = {
      {"3115502203""4c2500", "Reg"},
      {"3115502203""4d2500", "Generic"},  // wrong CRC: not Reg
      {"3115502203""a82600", "Reg26"},  // correct CRC, placeholder 00 in the definition
      {"3115502201""4c", "Short"},
    };
    for (const auto& prec : precedence) {
      MasterSymbolString master;
      master.parseHex(prec[0]);
      Message* message = messages->find(master);
      check(message && message->getName() == prec[1], string("precedence ") + prec[0] + " -> "
          + (message ? message->getName() : "none"));
    }
  }
  result = loadCsv(messages, csv, &err);
  struct {
    const char* master;
    const char* expectName;  // nullptr for no match
  } receives[] = {
    {"3115502203""4c2500", "BlockingTime"},
    {"3115502203""4d2500", nullptr},  // wrong CRC
    {"3115502203""ab2600", "Other"},  // the placeholder in the definition is not the CRC, but the CRC on the bus is
    {"3115502203""002700", "Plain"},
    {"ff15502305""cc25000200", "BlockingTime"},
    {"ff15502305""2825000100", "BlockingTime"},
    {"ff15502305""4c25000200", nullptr},  // a write with the CRC of the address only
    {"3108502203""cc2d00", "Lax"},  // check=ignore: a constant accepted
    {"3108502203""142d00", "Lax"},  // check=ignore: the correct one too
  };
  // the correct CRC of register 0x0026
  symbol_t crc26 = 0;
  {
    MasterSymbolString m;
    m.parseHex("3115502203""002600");
    wh.compute(m, &crc26);
  }
  for (const auto& recv : receives) {
    MasterSymbolString master;
    master.parseHex(recv.master);
    if (string(recv.master) == "3115502203""ab2600") {
      master[5] = crc26;
    }
    Message* message = messages->find(master);
    bool ok = recv.expectName == nullptr ? message == nullptr
        : message != nullptr && message->getName() == recv.expectName;
    check(ok, string("find ") + master.getStr() + " -> " + (message ? message->getName() : "none"));
  }
}

int main() {
  // message:   [type],[circuit],name,[comment],[QQ[;QQ]*],[ZZ],[PBSB],[ID],fields...
  // field:     name,part,type[:len][,[divisor|values][,[unit][,[comment]]]]
  // template:  name,type[:len][,[divisor|values][,[unit][,[comment]]]]
  // condition: name,circuit,messagename,[comment],[fieldname],[ZZ],values
  // "message", "decoded", "master", "slave", "flags"
  unsigned int baseLine = __LINE__+1;
  string checks[][5] = {
    {"date,HDA:3,,,Datum", "", "", "", "template"},
    {"bdate:date,BDA,,,Datum", "", "", "", "template"},
    {"time,VTI,,,", "", "", "", "template"},
    {"btime:time,BTI,,,Uhrzeit", "", "", "", "template"},
    {"dcfstate,UCH,0=nosignal;1=ok;2=sync;3=valid,,", "", "", "", "template"},
    {"temp,D2C,,°C,Temperatur", "", "", "", "template"},
    {"temp1,D1C,,°C,Temperatur", "", "", "", "template"},
    {"temp2,D2B,,°C,Temperatur", "", "", "", "template"},
    {"power,UCH,,kW", "", "", "", "template"},
    {"sensor,UCH,0=ok;85=circuit;170=cutoff,,Fühlerstatus", "", "", "", "template"},
    {"sensorc,UCH,=85,,Fühlerstatus", "", "", "", "template"},
    {"pumpstate,UCH,0=off;1=on;2=overrun,,Pumpenstatus", "", "", "", "template"},
    {"tempsensor,temp;sensor,,Temperatursensor", "", "", "", "template"},
    {"tempsensorc,temp;sensorc,,Temperatursensor", "", "", "", "template"},
    {"r,cir,Status01,VL/RL/AussenTemp/VLWW/SpeicherTemp/Status,,08,B511,01,,,temp1;temp1;temp2;temp1;temp1;pumpstate", "28.0;24.0;4.938;35.0;41.0;4", "ff08b5110101", "093830f00446520400ff", "d"},
    {"r,message_circuit,message_name,message comment,,25,B509,0d2800,,,tempsensor", "temp=-14.00 Temperatursensor [Temperatur];sensor=ok [Fühlerstatus]", "ff25b509030d2800", "0320ff00", "D"},
    {"r,message_circuit,message_name,message comment,,25,B509,0d2800,,,tempsensor,,field unit,field comment", "temp=-14.00 field unit [field comment];sensor=ok [Fühlerstatus]", "ff25b509030d2800", "0320ff00", "D"},
    {"r,message_circuit,message_name,message comment,,25,B509,0d2800,,,tempsensor,,field unit,field comment", "\n     \"temp\": {\"value\": -14.00},\n     \"sensor\": {\"value\": \"ok\"}", "ff25b509030d2800", "0320ff00", "j"},
    {"r,message_circuit,message_name,message comment,,25,B509,0d2800,,,tempsensor,,field unit,field comment", "\n     \"temp\": {\"value\": -14.00, \"unit\": \"field unit\", \"comment\": \"field comment\"},\n" "     \"sensor\": {\"value\": \"ok\", \"comment\": \"Fühlerstatus\"}", "ff25b509030d2800", "0320ff00", "J"},
    {"r,message_circuit,message_name,\"message, comment\",,25,B509,0d2800,,,tempsensor,,field unit,\"field, comment\"", "\n     \"temp\": {\"value\": -14.00, \"unit\": \"field unit\", \"comment\": \"field, comment\"},\n" "     \"sensor\": {\"value\": \"ok\", \"comment\": \"Fühlerstatus\"}", "ff25b509030d2800", "0320ff00", "J"},
    {"r,message_circuit,message_name,\"message\"\",\"\" comment\",,25,B509,0d2800,,,tempsensor,,field unit,\"field\"\",\"\" comment\"", "\n     \"temp\": {\"value\": -14.00, \"unit\": \"field unit\", \"comment\": \"field',' comment\"},\n" "     \"sensor\": {\"value\": \"ok\", \"comment\": \"Fühlerstatus\"}", "ff25b509030d2800", "0320ff00", "J"},
    {"r,message_circuit,message_name,message comment,,25,B509,0d2800,,,temp,,field unit,field comment,,,sensor", "temp=-14.00 field unit [field comment];sensor=ok [Fühlerstatus]", "ff25b509030d2800", "0320ff00", "D"},
    {"r,message_circuit,message_name,message comment,,25,B509,0d2800,,,temp,,field unit,\"field\"\",\"\" comment\",,,sensor", "temp=-14.00 field unit [field\",\" comment];sensor=ok [Fühlerstatus]", "ff25b509030d2800", "0320ff00", "D"},
    {"r,message_circuit,message_name,message comment,,25,B509,0d2800,,,D2C,,°C,Temperatur,,,sensor", "\n     \"0\": {\"name\": \"\", \"value\": -14.00},\n     \"1\": {\"name\": \"sensor\", \"value\": \"ok\"}", "ff25b509030d2800", "0320ff00", "j"},
    {"r,cir,name,,,25,B509,0d2800,,,tempsensorc", "-14.00", "ff25b509030d2800", "0320ff55", ""},
    {"r,cir,name,,,25,B509,0d28,,m,sensorc,,,,,,temp", "-14.00", "ff25b509030d2855", "0220ff", ""},
    {"u,cir,first,,,fe,0700,,x,,bda", "26.10.2014", "fffe07000426100714", "00", "p"},
    {"u,broadcast,hwStatus,,,fe,b505,27,,,UCH,,,,,,UCH,,,,,,UCH,,,", "0;19;0", "10feb505042700130097", "00", ""},
    {"u,broadcast,datetime,Datum/Uhrzeit,,fe,0700,,outsidetemp,,temp2,,°C,Aussentemperatur,time,,btime,,,,date,,BDA,,,Datum", "outsidetemp=14.500 °C [Aussentemperatur];time=12:25:01 [Uhrzeit];date=01.05.2017 [Datum]", "10fe070009800e01251201050117", "", "D"},
    {"u,broadcast,datetime,Datum Uhrzeit,,fe,0700,,,,temp2;btime;bdate", "temp2=14.500 °C [Temperatur];time=12:25:01 [Uhrzeit];date=01.05.2017 [Datum]", "10fe070009800e01251201050117", "", "D"},
    {"w,cir,first,,,15,b509,0400,date,,bda", "26.10.2014", "ff15b50906040026100714", "00", ""},
    {"w,cir,first,,,15,b509", "", "ff15b50900", "00", ""},
    {"*w,,,,,,b505,2d", "", "", "", ""},
    {"w,cir,offset,,,50,,,,,temp", "0.50", "ff50b505042d080000", "00", "kd"},
    {"r,ehp,time,,,08,b509,0d2800,,,time", "15:00:17", "ff08b509030d2800", "0311000f", "d"},
    {"r,ehp,time,,,08;10,b509,0d2800,,,time", "", "", "", "c"},
    {"r,ehp,time,,,08;09,b509,0d2800,,,time", "15:00:17", "ff08b509030d2800", "0311000f", "d*"},
    {"r,ehp,date,,,08,b509,0d2900,,,date", "23.11.2014", "ff08b509030d2900", "03170b0e", "d"},
    {"r,700,date,,,15,b524,020000003400,,,IGN:4,,,,,,date", "23.11.2015", "ff15b52406020000003400", "0703003400170b0f", "d"},
    {"", "23.11.2015", "ff15b52406020000003400", "0703003400170b0f", "kd"},
    {"r,700,time,,,15,b524,030000003500,,,IGN:4,,,,,,HTI", "12:29:06", "ff15b52406030000003500", "07030035000c1d06", "d"},
    {"", "12:29:06", "ff15b52406030000003500", "07030035000c1d06", "kd"},
    {"r,700,mupd,,,15,b524,030000000100,,m,UCH,,,,,,HTI", "1;12:29:07", "ff15b5240703000000010001", "030c1d07", "d"},
    {"", "2;12:29:07", "ff15b5240703000000010002", "030c1d07", "kdu"},
    {"", "2;12:29:07", "ff15b5240703000000010002", "030c1d07", "kdU"},
    {"w,700,date,,,15,b524,020000003400,,,date", "23.11.2015", "ff15b52409020000003400170b0f", "00", ""},
    {"r,ehp,error,,,08,b509,0d2800,index,m,UCH,,,,,,time", "3;15:00:17", "ff08b509040d280003", "0311000f", "di"},
    {"r,ehp,error,,,08,b509,0d2800,index,m,UCH,,,,,,time", "index=3;time=15:00:17", "ff08b509040d280003", "0311000f", "D"},
    {"u,ehp,ActualEnvironmentPower,Energiebezug,,08,B509,29BA00,,s,IGN:2,,,,,s,power", "8", "1008b5090329ba00", "03ba0008", "p"},
    {"uw,ehp,test,Test,,08,B5de,ab,,,power,,,,,s,hex:1", "8;39", "1008b5de02ab08", "0139", "p"},
    {"u,ehp,hwTankTemp,Speichertemperatur IST,,25,B509,290000,,,IGN:2,,,,,,tempsensor", "", "", "", "M"},
    {"", "55.50;ok", "1025b50903290000", "050000780300", "kd"},
    {"r,ehp,datetime,Datum Uhrzeit,,50,B504,00,,,dcfstate,,,,time,,BTI,,,,date,,BDA,,,,temp,,temp2", "valid;08:24:51;31.12.2014;-0.875", "1050b5040100", "0a035124083112031420ff", "d" },
    {"r,ehp,bad,invalid pos,,50,B5ff,000102,,m,HEX:8;tempsensor;tempsensor;tempsensor;tempsensor;power;power,,,", "", "", "", "c" },
    {"r,ehp,bad,invalid pos,,50,B5ff,,,s,HEX:8;tempsensor;tempsensor;tempsensor;tempsensor;tempsensor;power;power,,,", "", "", "", "c" },
    {"r,ehp,ApplianceCode,,,08,b509,0d4301,,,UCH,", "9", "ff08b509030d4301", "0109", "d" },
    {"*r,ehp,,,,08,b509,0d", "", "", "", "" },
    {"*w,ehp,,,,08,b509,0e", "", "", "", "" },
    {"*[brinetowater],ehp,ApplianceCode,,,,4;6;8;9;10", "", "", "", "" },
    {"[airtowater]r,ehp,notavailable,,,,,0100,,,uch", "1", "", "", "kc" },
    {"[brinetowater]r,ehp,available,,,,,0100,,,uch", "1", "ff08b509030d0100", "0101", "kd" },
    {"r,,x,,,,,\"6800\",,,UCH,,,bit0=\"comment, continued comment", "", "", "", "c" },
    {"r,,x,,,,,\"6800\",,,UCH,,\"\",\"bit0=\"comment, continued comment\"", "=1 [bit0=\"comment, continued comment]", "ff08b509030d6800", "0101", "D" },
    {"r,ehp,multi,,,,,0001:5;0002;0003,longname,,STR:15", "ABCDEFGHIJKLMNO", "ff08b509030d0001;ff08b509030d0003;ff08b509030d0002", "054142434445;054b4c4d4e4f;05464748494a", "dC" },
    {"r,ehp,multi,,,,,01;02;03,longname,,STR:15", "ABCDEFGHIJKLMNO", "ff08b509020d01;ff08b509020d03;ff08b509020d02", "084142434445464748;054b4c4d4e4f;02494a", "dC" },
    {"w,ehp,multi,,,,,01:8;02:2;03,longname,,STR:15", "ABCDEFGHIJKLMNO", "ff08b5090a0e014142434445464748;ff08b509040e02494a;ff08b509070e034b4c4d4e4f", "00;00;00", "dC" },
    {"w,ehp,multi,,,,,01:8;02:2;0304,longname,,STR:15", "ABCDEFGHIJKLMNO", "ff08b5090a0e014142434445464748;ff08b509040e02494a;ff08b509070e034b4c4d4e4f", "00;00;00", "cC" },
    {"r,ehp,scan,chained scan,,08,B509,24:9;25;26;27,,,IGN,,,,id4,,STR:28", "21074500100027790000000000N8", "ff08b5090124;ff08b5090125;ff08b5090126;ff08b5090127", "09003231303734353030;09313030303237373930;09303030303030303030;024E38", "dC" },
    {"r,,x,,,,,6900,,,UCH,10,bar,,Bit7,,BI7,0=B70;1=B71,,,Bit6,,BI6:1,0=B60;1=B61", "1.9;B71;B61", "ff08b509030d6900", "03138040", "d" },
    {"r,,x,,,,,6900,,,UCH,10,bar,,Bit7,,BI7,0=B70;1=B71,,,Bit6,,BI6:1,0=B60;1=B61", "1.9;B71;B60", "ff08b509030d6900", "0313ffbf", "d" },
    {"r,,x,,,,,6900,,,UCH,10,bar,,Bit7,,BI7,0=B70;1=B71,,,Bit6,,BI6:1,0=B60;1=B61", "1.9;B70;B61", "ff08b509030d6900", "03137fff", "d" },
    {"r,,x,,,,,6900,,,UCH,10,bar,,Bit7,,BI7,0=B70;1=B71,,,Bit6,,BI6:1,0=B60;1=B61", "1.9;B70;B60", "ff08b509030d6900", "03137fbf", "d" },
    {"r,,x,,,,,6a00,,,UCH,10,bar,,Bit6,,BI6:1,0=B60;1=B61,,,Bit7,,BI7,0=B70;1=B71", "1.9;B61;B71", "ff08b509030d6a00", "0213ff", "d" },
    {"r,,x,,,,,6a00,,,UCH,10,bar,,Bit6,,BI6:1,0=B60;1=B61,,,Bit7,,BI7,0=B70;1=B71", "1.9;B60;B71", "ff08b509030d6a00", "0213bf", "d" },
    {"r,,x,,,,,6a00,,,UCH,10,bar,,Bit6,,BI6:1,0=B60;1=B61,,,Bit7,,BI7,0=B70;1=B71", "1.9;B61;B70", "ff08b509030d6a00", "02137f", "d" },
    {"r,,x,,,,,6a00,,,UCH,10,bar,,Bit6,,BI6:1,0=B60;1=B61,,,Bit7,,BI7,0=B70;1=B71", "1.9;B60;B70", "ff08b509030d6a00", "02133f", "d" },
    {"w,,x,,,,,6900,,,UCH,10,bar,,Bit7,,BI7,0=B70;1=B71,,,Bit6,,BI6:1,0=B60;1=B61", "1.9;B71;B61", "ff08b509060e6900138040", "00", "di" },
    {"w,,x,,,,,6900,,,UCH,10,bar,,Bit7,,BI7,0=B70;1=B71,,,Bit6,,BI6:1,0=B60;1=B61", "1.9;B71;B60", "ff08b509060e6900138000", "00", "di" },
    {"w,,x,,,,,6900,,,UCH,10,bar,,Bit7,,BI7,0=B70;1=B71,,,Bit6,,BI6:1,0=B60;1=B61", "1.9;B70;B61", "ff08b509060e6900130040", "00", "di" },
    {"w,,x,,,,,6900,,,UCH,10,bar,,Bit7,,BI7,0=B70;1=B71,,,Bit6,,BI6:1,0=B60;1=B61", "1.9;B70;B60", "ff08b509060e6900130000", "00", "di" },
    {"w,,x,,,,,6a00,,,UCH,10,bar,,Bit6,,BI6:1,0=B60;1=B61,,,Bit7,,BI7,0=B70;1=B71", "1.9;B61;B71", "ff08b509050e6a0013c0", "00", "di" },
    {"w,,x,,,,,6a00,,,UCH,10,bar,,Bit6,,BI6:1,0=B60;1=B61,,,Bit7,,BI7,0=B70;1=B71", "1.9;B60;B71", "ff08b509050e6a001380", "00", "di" },
    {"w,,x,,,,,6a00,,,UCH,10,bar,,Bit6,,BI6:1,0=B60;1=B61,,,Bit7,,BI7,0=B70;1=B71", "1.9;B61;B70", "ff08b509050e6a001340", "00", "di" },
    {"w,,x,,,,,6a00,,,UCH,10,bar,,Bit6,,BI6:1,0=B60;1=B61,,,Bit7,,BI7,0=B70;1=B71", "1.9;B60;B70", "ff08b509050e6a001300", "00", "di" },
    {"w,,x,,,,,,,,IGN:1,,,,b0,,BI0:1,,,,b1,,BI1:1,,,,b2,,BI2:1,,,,,,IGN:1,,,,c0,,BI0:1,,,,c1,,BI1:1,,,,c2,,BI2:1", "1;1;1;0;0;0", "ff08b509050e00070000", "00", "di" },
    {"w,,x,,,,,,,,IGN:1,,,,b0,,BI0:1,,,,b1,,BI1:1,,,,b2,,BI2:1,,,,,,IGN:1,,,,c0,,BI0:1,,,,c1,,BI1:1,,,,c2,,BI2:1", "1;0;0;0;0;1", "ff08b509050e00010004", "00", "di" },
    {"w,,x,,,,,,,,IGN:1,,,,b0,,BI0:1,,,,b1,,BI1:1,,,,b2,,BI2:1,,,,,,IGN:1,,,,c0,,BI0:1,,,,c1,,BI1:1,,,,c2,,BI2:1", "0;0;1;0;1;1", "ff08b509050e00040006", "00", "di" },
    {"w,,x,,,,,,b0,,BI0:1,,,,b1,,BI1:1,,,,b2,,BI2:6,,,,c0,,BI0:1,,,,c1,,BI1:1,,,,c2,,BI2:1", "1;1;1;0;0;0", "ff08b509030e0700", "00", "di" },
    {"w,,x,,,,,,b0,,BI0:1,,,,b1,,BI1:1,,,,b2,,BI2:6,,,,c0,,BI0:1,,,,c1,,BI1:1,,,,c2,,BI2:1", "1;0;0;0;0;1", "ff08b509030e0104", "00", "di" },
    {"w,,x,,,,,,b0,,BI0:1,,,,b1,,BI1:1,,,,b2,,BI2:6,,,,c0,,BI0:1,,,,c1,,BI1:1,,,,c2,,BI2:1", "0;0;1;0;1;1", "ff08b509030e0406", "00", "di" },
    {"r,470,ccTimer.Monday,,,15,B515,0002,,,IGN:1,,,,from,,TTM", "", "", "", "M"},
    {"w,470,ccTimer.Monday,,,10,B515,0002,from,,TTM", "", "", "", "kM*"},
    {"", "19:00", "3115b515020002", "080272", "kd"},
    {"", "19:00", "3110b51503000272", "00", "kd"},
    {"*r,cir*cuit#level,na*me,com*ment,ff,75,b509,0d", "", "", "", ""},
    {"r,CIRCUIT,NAME,COMMENT,,,,0100,field,,UCH", "r,cirCIRCUITcuit,naNAMEme,comCOMMENTment,ff,75,b509,0d0100,field,s,UCH,,,: field=42", "ff75b509030d0100", "012a", "DN"},
    {"r,CIRCUIT,NAME,COMMENT,,,,0100,field,,UCH", "r,cirCIRCUITcuit,naNAMEme,comCOMMENTment,ff,75,b509,0d0100,field,s,UCH,,,: [b5090d0100/2a] field=[2a]42", "ff75b509030d0100", "012a", "DNr"},
    {"r,CIRCUIT,NAME,COMMENT,,,,0100,field,,UCH",
      "   \"naNAMEme\": {\n"
      "    \"name\": \"naNAMEme\",\n"
      "    \"passive\": false,\n"
      "    \"write\": false,\n"
      "    \"lastup\": *,\n"
      "    \"qq\": 255,\n"
      "    \"zz\": 117,\n"
      "    \"id\": [181, 9, 13, 1, 0],\n"
      "    \"fields\": {\n"
      "     \"field\": {\"value\": 42}\n"
      "    },\n"
      "    \"fielddefs\": [\n"
      "     { \"name\": \"field\", \"slave\": true, \"type\": \"UCH\", \"isbits\": false, \"length\": 1, \"unit\": \"\", \"comment\": \"\"}\n"
      "    ]\n"
      "   }: \n"
      "     \"field\": {\"value\": 42}", "ff75b509030d0100", "012a", "jN"},
    {"r,CIRCUIT,NAME,COMMENT,,,,0100,field,,UCH",
      "   \"naNAMEme\": {\n"
      "    \"name\": \"naNAMEme\",\n"
      "    \"passive\": false,\n"
      "    \"write\": false,\n"
      "    \"lastup\": *,\n"
      "    \"qq\": 255,\n"
      "    \"zz\": 117,\n"
      "    \"id\": [181, 9, 13, 1, 0],\n"
      "    \"master\": [255, 117, 181, 9, 3, 13, 1, 0],\n"
      "    \"slave\": [1, 42],\n"
      "    \"fields\": {\n"
      "     \"field\": {\"value\": 42, \"raw\": [42]}\n"
      "    },\n"
      "    \"fielddefs\": [\n"
      "     { \"name\": \"field\", \"slave\": true, \"type\": \"UCH\", \"isbits\": false, \"length\": 1, \"unit\": \"\", \"comment\": \"\"}\n"
      "    ]\n"
      "   }: \n"
      "     \"field\": {\"value\": 42, \"raw\": [42]}", "ff75b509030d0100", "012a", "jNr"},
    {"r,CIRCUIT,NAME,COMMENT,,,,0100,field,,temp", "r,cirCIRCUITcuit,naNAMEme,comCOMMENTment,ff,75,b509,0d0100,field,s,D2C,,°C,Temperatur: field=42.00 °C [Temperatur]", "ff75b509030d0100", "02a002", "DN"},
    {"r,CIRCUIT,NAME,COMMENT,,,,0100,field,,D2C,,°C,Temperatur", "r,cirCIRCUITcuit,naNAMEme,comCOMMENTment,ff,75,b509,0d0100,field,s,D2C,,°C,Temperatur: field=42.00 °C [Temperatur]", "ff75b509030d0100", "02a002", "DN"},
  };
  templates = new DataFieldTemplates();
  unsigned int lineNo = 0;
  istringstream dummystr("#");
  string errorDescription;
  vector<string> row;
  templates->readLineFromStream(&dummystr, __FILE__, false, &lineNo, &row, &errorDescription, false, nullptr, nullptr);
  lineNo = 0;
  MessageMap* messages = new MessageMap("");
  messages->setResolver(new TestResolver());
  dummystr.clear();
  dummystr.str("#");
  messages->readLineFromStream(&dummystr, __FILE__, false, &lineNo, &row, &errorDescription, false, nullptr, nullptr);
  vector< vector<string> > defaultsRows;
  Message* message = nullptr;
  vector<MasterSymbolString*> mstrs;
  vector<SlaveSymbolString*> sstrs;
  mstrs.resize(1);
  sstrs.resize(1);
  for (unsigned int i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
    string* check = checks[i];
    string inputStr = check[1];
    string flags = check[4];
    bool isTemplate = flags == "template";
    bool keepMessages = flags.find('k') != string::npos;
    bool checkUpdateTime = flags.find('u') != string::npos;
    bool checkSameChangeTime = flags.find('U') != string::npos;
    bool onlyMap = flags.find('M') != string::npos;
    bool failedCreate = flags.find('c') != string::npos;
    bool isChain = flags.find('C') != string::npos;
    bool decodeJson = flags.find('j') != string::npos || flags.find('J') != string::npos;
    bool decodeVerbose = flags.find('D') != string::npos || flags.find('J') != string::npos;
    bool withMessageDump = flags.find('N') != string::npos;
    bool decode = decodeJson || decodeVerbose || (flags.find('d') != string::npos);
    OutputFormat verbosity = (decodeVerbose?OF_NAMES|OF_UNITS|OF_COMMENTS:OF_NONE)|(decodeJson?OF_NAMES|OF_JSON:OF_NONE);
    if (flags.find('r') != string::npos) {
      verbosity |= OF_RAWDATA;
    }
    bool failedPrepare = flags.find('p') != string::npos;
    bool failedPrepareMatch = flags.find('P') != string::npos;
    bool multi = flags.find('*') != string::npos;
    bool withInput = flags.find('i') != string::npos;
    result_t result = RESULT_EMPTY;
    istringstream isstr(check[0]);
    lineNo = baseLine + i;
    cout << "line " << (lineNo+1) << " ";
    if (isTemplate) {
      result = templates->readLineFromStream(&isstr, __FILE__, false, &lineNo, &row, &errorDescription, false, nullptr, nullptr);
      if (result != RESULT_OK) {
        cout << "\"" << check[0] << "\": template read error: " << getResultCode(result) << ", " << errorDescription
            << endl;
        error = true;
      }
      cout << "\"" << check[0] << "\": template read OK" << endl;
      continue;
    }
    if (!keepMessages) {
      messages->clear();
    }
    if (isstr.peek() == '*') {
      // store defaults or condition
      result = messages->readLineFromStream(&isstr, __FILE__, false, &lineNo, &row, &errorDescription, false, nullptr, nullptr);
      if (result != RESULT_OK) {
        cout << "\"" << check[0] << "\": default read error: " << getResultCode(result) << ", " << errorDescription << endl;
        error = true;
        continue;
      }
      cout << "\"" << check[0] << "\": default read OK" << endl;
      continue;
    }
    if (isChain) {
      size_t pos = 0;
      string token;
      istringstream stream(check[2]);
      while (getline(stream, token, VALUE_SEPARATOR)) {
        if (pos >= mstrs.size()) {
          mstrs.resize(pos+1);
        } else if (mstrs[pos] != nullptr) {
          delete mstrs[pos];
        }
        mstrs[pos] = new MasterSymbolString();
        result = mstrs[pos]->parseHex(token);
        if (result != RESULT_OK) {
          cout << "\"" << check[0] << "\": parse \"" << token << "\" error: " << getResultCode(result) << endl;
          error = true;
          break;
        }
        pos++;
      }
      pos = 0;
      stream.str(check[3]);
      stream.clear();
      while (getline(stream, token, VALUE_SEPARATOR)) {
        if (pos >= sstrs.size()) {
          sstrs.resize(pos+1);
        } else if (sstrs[pos] != nullptr) {
          delete sstrs[pos];
        }
        sstrs[pos] = new SlaveSymbolString();
        result = sstrs[pos]->parseHex(token);
        if (result != RESULT_OK) {
          cout << "\"" << check[0] << "\": parse \"" << token << "\" error: " << getResultCode(result) << endl;
          error = true;
          break;
        }
        pos++;
      }
      if (result != RESULT_OK) {
        error = true;
        continue;
      }
    } else {
      if (mstrs[0] != nullptr) {
        delete mstrs[0];
      }
      mstrs[0] = new MasterSymbolString();
      result = mstrs[0]->parseHex(check[2]);
      if (result != RESULT_OK) {
        cout << "\"" << check[0] << "\": parse \"" << check[2] << "\" error: " << getResultCode(result) << endl;
        error = true;
        continue;
      }
      if (sstrs[0] != nullptr) {
        delete sstrs[0];
      }
      sstrs[0] = new SlaveSymbolString();
      result = sstrs[0]->parseHex(check[3]);
      if (result != RESULT_OK) {
        cout << "\"" << check[0] << "\": parse \"" << check[3] << "\" error: " << getResultCode(result) << endl;
        error = true;
        continue;
      }
    }

    if (isstr.peek() == EOF) {
      message = messages->find(*mstrs[0]);
      if (message == nullptr) {
        cout << "\"" << check[2] << "\": find error: nullptr" << endl;
        error = true;
        continue;
      }
      cout << "\"" << check[2] << "\": find OK" << endl;
    } else {
      result = messages->readLineFromStream(&isstr, __FILE__, false, &lineNo, &row, &errorDescription, false, nullptr, nullptr);
      if (failedCreate) {
        if (result == RESULT_OK) {
          cout << "\"" << check[0] << "\": failed create error: unexpectedly succeeded" << endl;
          error = true;
        } else {
          cout << "\"" << check[0] << "\": failed create OK" << endl;
        }
        continue;
      }
      if (result != RESULT_OK) {
        cout << "\"" << check[0] << "\": create error: " << getResultCode(result) << ", " << errorDescription << endl;
        error = true;
        continue;
      }
      if (messages->size() == 0) {
        cout << "\"" << check[0] << "\": create error: nullptr" << endl;
        error = true;
        continue;
      }
      if (multi && messages->size() == 1) {
        cout << "\"" << check[0] << "\": create error: single message instead of multiple" << endl;
        error = true;
        continue;
      }
      if (!multi && messages->size() > 1) {
        cout << "\"" << check[0] << "\": create error: multiple messages instead of single" << endl;
        error = true;
        continue;
      }
      cout << "\"" << check[0] << "\": create OK" << endl;
      if (onlyMap) {
        continue;
      }
      deque<Message*> msgs;
      messages->findAll("", "", "*", false, true, true, true, true, false, 0, 0, false, &msgs);
      if (msgs.empty()) {
        message = nullptr;
        cout << "\"" << check[0] << "\": create error: message not found" << endl;
        error = true;
        continue;
      }
      message = *msgs.begin();
      Message* foundMessage = messages->find(*mstrs[0], false, true, true, true, false);
      if (foundMessage == message) {
        cout << "  find OK" << endl;
      } else if (foundMessage == nullptr) {
        cout << "  find error: message not found by master " << mstrs[0]->getStr() << endl;
        error = true;
        continue;
      } else {
        cout << "  find error: different" << endl;
        error = true;
      }
    }

    if (message->isPassive() || decode) {
      time_t lastUpdateTime = message->getLastUpdateTime();
      time_t lastChangeTime = message->getLastChangeTime();
      if (checkUpdateTime || checkSameChangeTime) {
        sleep(2);
      }
      for (size_t index = 0; index < message->getCount(); index++) {
        message->storeLastData(*mstrs[index], *sstrs[index]);
      }
      ostringstream output;
      if (withMessageDump) {
        if (decodeJson) {
          message->decodeJson(false, false, true, verbosity|OF_DEFINITION, &output);
          string str = output.str();
          size_t start = str.find("\"lastup\": ");
          if (start != string::npos) {
            start += 10;
            size_t end = str.find(",", start);
            if (end != string::npos) {
              str = str.substr(0, start)+"*"+str.substr(end);
            }
          }
          output.str("");
          output << str;
        } else {
          message->dump(nullptr, true, OF_NONE, &output);
        }
        output << ": ";
      }
      result = message->decodeLastData(pt_any, false, nullptr, -1, verbosity, &output);
      if (result != RESULT_OK) {
        cout << "  \"" << check[2] << "\" / \"" << check[3] << "\": decode error " << (message->isWrite() ? "write: " : "read: ")
            << getResultCode(result) << endl;
        error = true;
        continue;
      }
      cout << "  \"" << check[2] << "\" / \"" << check[3] <<  "\": decode OK" << endl;
      string outStr = output.str();
      bool match = inputStr == output.str();
      verify(false, "decode", check[2] + "/" + check[3], match, inputStr, output.str());
      if (checkUpdateTime || checkSameChangeTime) {
        time_t time = message->getLastUpdateTime();
        if (time == lastUpdateTime) {
          cout << "  update time error: not updated" << endl;
        } else {
          cout << "  update time OK" << endl;
        }
        time = message->getLastChangeTime();
        if (checkSameChangeTime) {
          if (time != lastChangeTime) {
            cout << "  same change time error: unexpectedly updated" << endl;
          } else {
            cout << "  same change time OK" << endl;
          }
        } else {
          if (time == lastChangeTime) {
            cout << "  change time error: not updated" << endl;
          } else {
            cout << "  change time OK" << endl;
          }
        }
      }
    }
    if (!message->isPassive() && (withInput || !decode)) {
      istringstream input(inputStr);
      MasterSymbolString writeMstr;
      result = message->prepareMaster(0, 0xff, SYN, UI_FIELD_SEPARATOR, &input, &writeMstr);
      if (failedPrepare) {
        if (result == RESULT_OK) {
          cout << "  \"" << inputStr << "\": failed prepare error: unexpectedly succeeded" << endl;
          error = true;
        } else {
          cout << "  \"" << inputStr << "\": failed prepare OK" << endl;
        }
        continue;
      }

      if (result != RESULT_OK) {
        cout << "  \"" << inputStr << "\": prepare error: " << getResultCode(result) << endl;
        error = true;
        continue;
      }
      cout << "  \"" << inputStr << "\": prepare OK" << endl;

      bool match = writeMstr == *mstrs[0];
      verify(failedPrepareMatch, "prepare", inputStr, match, mstrs[0]->getStr(), writeMstr.getStr());
    }
  }

  testComputedByte(messages);

  testLongId(messages);

  testFills(messages);

  delete templates;
  delete messages;
  for (vector<MasterSymbolString*>::iterator it = mstrs.begin(); it != mstrs.end(); it++) {
    delete *it;
  }
  for (vector<SlaveSymbolString*>::iterator it = sstrs.begin(); it != sstrs.end(); it++) {
    delete *it;
  }
  return error ? 1 : 0;
}
