// Copyright 2025 Adrian Gjerstad
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// -----------------------------------------------------------------------------
// File: dns.cc
// -----------------------------------------------------------------------------
//
// This file implements a DNS/hostname resolver.
//

#include "conduit/net/dns.h"

#include <arpa/inet.h>
#include <strings.h>

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

#include "conduit/conduit.h"
#include "conduit/net/net.h"
#include "conduit/net/socket.h"
#include "conduit/promise.h"
#include "conduit/timer.h"

namespace cd {

DNSName::DNSName(absl::string_view name) : name_(name) {
  // Nothing to do.
}

const std::string& DNSName::Name() const {
  return name_;
}

void DNSName::Name(absl::string_view name) {
  name_ = name;
}

void DNSName::Serialize(std::ostringstream* os,
                        absl::flat_hash_map<std::string, int>* comp_map) const {
  std::vector<absl::string_view> labels = absl::StrSplit(name_, '.');
  absl::string_view search(name_);

  for (const auto& label : labels) {
    // If we could find this domain in the compression map, that would mean we
    // can emit a compression label.
    if (comp_map->contains(search)) {
      // Cool, just emit a compression label and return.
      uint16_t clabel = htons(0xC000 | (comp_map->at(search)));
      *os << std::string((char*)&clabel, sizeof(clabel));
      return;
    }

    // No luck :(
    comp_map->emplace(std::string(search), os->str().size());
    *os << static_cast<uint8_t>(label.size()) << label;
    search.remove_prefix(label.size());
    if (search.size() > 1) {
      search.remove_prefix(1);
    }
  }

  // We never found a compression label. We have to emit a null.
  *os << '\x00';
}

absl::StatusOr<DNSName> DNSName::Deserialize(absl::string_view* s,
  absl::string_view msg) {
  std::string name;
  // Get current location in message.
  size_t ptr = msg.size() - s->size();
  // True if we haven't jumped because of a compression label. False after the
  // first jump.
  bool consuming_original_label = true;
  // Number of bytes to run with remove_prefix.
  size_t remove_from_stream = 0;

  while (true) {
    // Start of a label. Let's parse it (safely)
    if (ptr >= msg.size()) {
      return absl::InvalidArgumentError("truncated message");
    }

    uint8_t x = static_cast<uint8_t>(msg[ptr]);
    if (x != 0x00 && ptr + 1 >= msg.size()) {
      // Not an ending byte. There should be at least one more byte here, but
      // there's not.
      return absl::InvalidArgumentError("truncated message");
    }

    if ((x & 0xC0) == 0xC0) {
      // Compression label
      uint16_t clabel = (((uint16_t)x & 0x3F) << 8) |
                        static_cast<uint8_t>(msg[ptr + 1]);

      if (consuming_original_label) remove_from_stream += 2;
      if (clabel >= msg.size() - s->size()) {
        // Potential parsing loop. Attempt to jump to future byte.
        return absl::InvalidArgumentError(
          "compression label attempts to jump forward"
        );
      }

      consuming_original_label = false;
      ptr = clabel;
      continue;
    }

    if (x > 63) {
      // Label too long. Not allowed.
      return absl::InvalidArgumentError("label too long");
    }

    if (x == 0) {
      // End condition.
      if (consuming_original_label) remove_from_stream += 1;
      break;
    }

    if (ptr + 1 + x >= msg.size()) {
      return absl::InvalidArgumentError("truncated message");
    }

    absl::string_view label(msg);
    label.remove_prefix(ptr + 1);
    label.remove_suffix((msg.size() - ptr) - 1 - x);
    name.append(label);
    name.push_back('.');
    if (consuming_original_label) remove_from_stream += 1 + x;
    ptr += x + 1;
  }

  s->remove_prefix(remove_from_stream);

  // Trailing '.'
  name.pop_back();
  return DNSName(name);
}

std::ostream& operator<<(std::ostream& os, const DNSName& name) {
  os << name.Name();
  return os;
}

DNSMailExchange::DNSMailExchange(uint16_t priority, const DNSName& name) :
  priority_(priority), name_(name) {
  // Nothing to do.
}

uint16_t DNSMailExchange::Priority() const {
  return priority_;
}

void DNSMailExchange::Priority(uint16_t priority) {
  priority_ = priority;
}

const DNSName& DNSMailExchange::Name() const {
  return name_;
}

void DNSMailExchange::Name(const DNSName& name) {
  name_ = name;
}

void DNSMailExchange::Serialize(std::ostringstream* os,
  absl::flat_hash_map<std::string, int>* comp_map) const {
  uint16_t serial = htons(priority_);
  *os << std::string((char*)&serial, sizeof(serial));
  name_.Serialize(os, comp_map);
}

absl::StatusOr<DNSMailExchange> DNSMailExchange::Deserialize(
  absl::string_view* s, absl::string_view msg) {
  if (s->size() < 2) {
    return absl::InvalidArgumentError("packet truncated");
  }

  uint16_t priority = (((uint16_t)static_cast<uint8_t>((*s)[0])) << 8) |
                                  static_cast<uint8_t>((*s)[1]);
  s->remove_prefix(2);

  absl::StatusOr<DNSName> name_s = DNSName::Deserialize(s, msg);
  if (!name_s.ok()) {
    return name_s.status();
  }

  return DNSMailExchange(priority, name_s.value());
}

std::ostream& operator<<(std::ostream& os, const DNSMailExchange& mx) {
  os << mx.Priority() << ' ' << mx.Name();
  return os;
}

DNSStartOfAuthority::DNSStartOfAuthority(const DNSName& auth_ns,
  absl::string_view email, uint32_t serial, absl::Duration refresh,
  absl::Duration retry, absl::Duration expire, absl::Duration default_ttl) :
  auth_ns_(auth_ns), email_(email), serial_(serial), refresh_(refresh),
  retry_(retry), expire_(expire), default_ttl_(default_ttl) {
  // Nothing to do.
}

const DNSName& DNSStartOfAuthority::AuthoritativeNS() const {
  return auth_ns_;
}

void DNSStartOfAuthority::AuthoritativeNS(const DNSName& auth_ns) {
  auth_ns_ = auth_ns;
}

const std::string& DNSStartOfAuthority::Email() const {
  return email_;
}

void DNSStartOfAuthority::Email(absl::string_view email) {
  email_ = std::string(email);
}

uint32_t DNSStartOfAuthority::Serial() const {
  return serial_;
}

void DNSStartOfAuthority::Serial(uint32_t serial) {
  serial_ = serial;
}

absl::Duration DNSStartOfAuthority::RefreshInterval() const {
  return refresh_;
}

void DNSStartOfAuthority::RefreshInterval(absl::Duration refresh) {
  refresh_ = refresh;
}

absl::Duration DNSStartOfAuthority::RetryDelay() const {
  return retry_;
}

void DNSStartOfAuthority::RetryDelay(absl::Duration retry) {
  retry_ = retry;
}

absl::Duration DNSStartOfAuthority::ExpirationTime() const {
  return expire_;
}

void DNSStartOfAuthority::ExpirationTime(absl::Duration expire) {
  expire_ = expire;
}

absl::Duration DNSStartOfAuthority::DefaultTTL() const {
  return default_ttl_;
}

void DNSStartOfAuthority::DefaultTTL(absl::Duration default_ttl) {
  default_ttl_ = default_ttl;
}

void DNSStartOfAuthority::Serialize(std::ostringstream* os,
  absl::flat_hash_map<std::string, int>* comp_map) const {
  uint32_t refresh = htonl(absl::ToInt64Seconds(refresh_));
  uint32_t retry = htonl(absl::ToInt64Seconds(retry_));
  uint32_t expire = htonl(absl::ToInt64Seconds(expire_));
  uint32_t default_ttl = htonl(absl::ToInt64Seconds(default_ttl_));

  auth_ns_.Serialize(os, comp_map);

  std::string email_name(email_);
  auto pos = email_name.find('@');
  if (pos != std::string::npos) {
    email_name[pos] = '.';
  }
  DNSName email(email_name);

  email.Serialize(os, comp_map);

  *os << std::string((const char*)&serial_, sizeof(serial_));
  *os << std::string((char*)&refresh, sizeof(refresh));
  *os << std::string((char*)&retry, sizeof(retry));
  *os << std::string((char*)&expire, sizeof(expire));
  *os << std::string((char*)&default_ttl, sizeof(default_ttl));
}

absl::StatusOr<DNSStartOfAuthority> DNSStartOfAuthority::Deserialize(
  absl::string_view* s, absl::string_view msg) {
  absl::StatusOr<DNSName> auth_ns_s = DNSName::Deserialize(s, msg);
  if (!auth_ns_s.ok()) {
    return auth_ns_s.status();
  }

  absl::StatusOr<DNSName> email_s = DNSName::Deserialize(s, msg);
  if (!email_s.ok()) {
    return email_s.status();
  }

  std::string email(email_s.value().Name());
  auto pos = email.find('.');
  if (pos != std::string::npos) {
    email[pos] = '@';
  }

  // Make sure we have enough room for the remainder of the record
  if (s->size() < 16) {
    return absl::InvalidArgumentError("truncated message");
  }

#define READ_U32(name) \
  uint32_t name =\
    (((uint32_t)static_cast<uint8_t>((*s)[0])) << 24) |\
    (((uint32_t)static_cast<uint8_t>((*s)[1])) << 16) |\
    (((uint32_t)static_cast<uint8_t>((*s)[2])) <<  8) |\
     ((uint32_t)static_cast<uint8_t>((*s)[3]));\
  s->remove_prefix(4)
#define READ_DURATION_U32(name) \
  READ_U32(name##_seconds);\
  absl::Duration name = absl::Seconds(name##_seconds)

  READ_U32(serial);
  READ_DURATION_U32(refresh);
  READ_DURATION_U32(retry);
  READ_DURATION_U32(expire);
  READ_DURATION_U32(default_ttl);
#undef READ_DURATION_U32
#undef READ_U32

  return DNSStartOfAuthority(auth_ns_s.value(), email, serial, refresh, retry,
    expire, default_ttl);
}

std::ostream& operator<<(std::ostream& os, const DNSStartOfAuthority& soa) {
  os << soa.AuthoritativeNS() << " <" << soa.Email()
     << "> serial " << soa.Serial()
     << " refresh " << soa.RefreshInterval()
     << " retry " << soa.RetryDelay()
     << " expire " << soa.ExpirationTime()
     << " default TTL " << soa.DefaultTTL();
  return os;
}

std::string StringifyDNSRecordType(const DNSRecordType& type) {
  switch (type) {
  case DNSRecordType::kA: return "A";
  case DNSRecordType::kNS: return "NS";
  case DNSRecordType::kCNAME: return "CNAME";
  case DNSRecordType::kSOA: return "SOA";
  case DNSRecordType::kPTR: return "PTR";
  case DNSRecordType::kMX: return "MX";
  case DNSRecordType::kTXT: return "TXT";
  case DNSRecordType::kAAAA: return "AAAA";
  case DNSRecordType::kLOC: return "LOC";
  case DNSRecordType::kSRV: return "SRV";
  case DNSRecordType::kNAPTR: return "NAPTR";
  case DNSRecordType::kDS: return "DS";
  case DNSRecordType::kIPSECKEY: return "IPSECKEY";
  case DNSRecordType::kRRSIG: return "RRSIG";
  case DNSRecordType::kNSEC: return "NSEC";
  case DNSRecordType::kDNSKEY: return "DNSKEY";
  case DNSRecordType::kNSEC3: return "NSEC3";
  case DNSRecordType::kNSEC3PARAM: return "NSEC3PARAM";
  case DNSRecordType::kAXFR: return "AXFR";
  case DNSRecordType::kMAILB: return "MAILB";
  case DNSRecordType::kAll: return "*";
  default: return "<unknown>";
  }
}

std::ostream& operator<<(std::ostream& os, const DNSRecordType& t) {
  os << StringifyDNSRecordType(t);
  return os;
}

std::string StringifyDNSRecordClass(const DNSRecordClass& c) {
  switch (c) {
  case DNSRecordClass::kIN: return "IN";
  case DNSRecordClass::kAll: return "*";
  default: return "<unknown>";
  }
}

std::ostream& operator<<(std::ostream& os, const DNSRecordClass& c) {
  os << StringifyDNSRecordClass(c);
  return os;
}

DNSQuestion::DNSQuestion(const DNSName& name, DNSRecordType type,
  DNSRecordClass r_class) : name_(name), type_(type), class_(r_class) {
  // Nothing to do.
}

absl::StatusOr<DNSQuestion> DNSQuestion::Deserialize(absl::string_view* pkt,
  absl::string_view msg) {
  absl::StatusOr<DNSName> name_s = DNSName::Deserialize(pkt, msg);
  if (!name_s.ok()) {
    return name_s.status();
  }
  auto name = name_s.value();

  if (pkt->size() < 4) {
    return absl::InvalidArgumentError("truncated message");
  }

  int type_hi = static_cast<uint8_t>((*pkt)[0]);
  int type_lo = static_cast<uint8_t>((*pkt)[1]);
  int class_hi = static_cast<uint8_t>((*pkt)[2]);
  int class_lo = static_cast<uint8_t>((*pkt)[3]);
  pkt->remove_prefix(4);

  DNSRecordType type((DNSRecordType)((type_hi << 8) | type_lo));
  DNSRecordClass r_class((DNSRecordClass)((class_hi << 8) | class_lo));

  return DNSQuestion(name, type, r_class);
}

void DNSQuestion::Serialize(std::ostringstream* os,
  absl::flat_hash_map<std::string, int>* comp_max) const {
  name_.Serialize(os, comp_max);

  unsigned char type_hi = ((unsigned int)type_) >> 8;
  unsigned char type_lo = ((unsigned int)type_) & 0xFF;
  unsigned char class_hi = ((unsigned int)class_) >> 8;
  unsigned char class_lo = ((unsigned int)class_) & 0xFF;

  *os << type_hi << type_lo << class_hi << class_lo;
}

std::ostream& operator<<(std::ostream& os, const DNSQuestion& q) {
  os << q.Name() << ": type " << q.Type() << ", class " << q.Class();
  return os;
}

const DNSName& DNSQuestion::Name() const {
  return name_;
}

void DNSQuestion::Name(const DNSName& name) {
  name_ = name;
}

DNSRecordType DNSQuestion::Type() const {
  return type_;
}

void DNSQuestion::Type(DNSRecordType t) {
  type_ = t;
}

DNSRecordClass DNSQuestion::Class() const {
  return class_;
}

void DNSQuestion::Class(DNSRecordClass c) {
  class_ = c;
}


absl::StatusOr<DNSRecord> DNSRecord::Deserialize(absl::string_view* pkt,
  absl::string_view msg) {
  // RR format:
  // Referring name (variable)
  // Record type (2 bytes)
  // Record class (2 bytes)
  // Time to Live (4 bytes)
  // Content length (2 bytes)
  // Content... (as per content length)
  absl::StatusOr<DNSName> name_s = DNSName::Deserialize(pkt, msg);
  if (!name_s.ok()) {
    return name_s.status();
  }

  if (pkt->size() < 10) {
    return absl::InvalidArgumentError("packet truncated");
  }
  DNSRecordType type = (DNSRecordType)(
                       ((uint16_t)(static_cast<uint8_t>((*pkt)[0])) << 8) |
                                   static_cast<uint8_t>((*pkt)[1]));
  DNSRecordClass c = (DNSRecordClass)(
                     ((uint16_t)(static_cast<uint8_t>((*pkt)[2])) << 8) |
                                 static_cast<uint8_t>((*pkt)[3]));
  uint32_t ttl_s = ((uint32_t)(static_cast<uint8_t>((*pkt)[4])) << 24) |
                   ((uint32_t)(static_cast<uint8_t>((*pkt)[5])) << 16) |
                   ((uint32_t)(static_cast<uint8_t>((*pkt)[6])) <<  8) |
                               static_cast<uint8_t>((*pkt)[7]);
  uint16_t rdlen = ((uint16_t)(static_cast<uint8_t>((*pkt)[8])) << 8) |
                               static_cast<uint8_t>((*pkt)[9]);
  pkt->remove_prefix(10);
  absl::Duration ttl = absl::Seconds(ttl_s);

  if (pkt->size() < rdlen) {
    return absl::InvalidArgumentError("packet truncated");
  }

  absl::string_view content(*pkt);
  pkt->remove_prefix(rdlen);
  switch (type) {
  case DNSRecordType::kA: {
    content.remove_suffix(content.size() - rdlen);
    absl::StatusOr<IPAddress> ip = IPAddress::FromBytes(content);
    if (!ip.ok()) {
      return ip.status();
    }
    return DNSRecord(name_s.value(), type, c, ttl, ip.value());
  }
  case DNSRecordType::kCNAME: {
    absl::StatusOr<DNSName> dname = DNSName::Deserialize(&content, msg);
    if (!dname.ok()) {
      return dname.status();
    }
    return DNSRecord(name_s.value(), type, c, ttl, dname.value());
  }
  case DNSRecordType::kMX: {
    absl::StatusOr<DNSMailExchange> mx =
      DNSMailExchange::Deserialize(&content, msg);
    if (!mx.ok()) {
      return mx.status();
    }
    return DNSRecord(name_s.value(), type, c, ttl, mx.value());
  }
  case DNSRecordType::kAAAA: {
    content.remove_suffix(content.size() - rdlen);
    absl::StatusOr<IPAddress> ip = IPAddress::FromBytes6(content);
    if (!ip.ok()) {
      return ip.status();
    }
    return DNSRecord(name_s.value(), type, c, ttl, ip.value());
  }
  case DNSRecordType::kSOA: {
    absl::StatusOr<DNSStartOfAuthority> soa = DNSStartOfAuthority::Deserialize(
      &content, msg
    );
    if (!soa.ok()) {
      return soa.status();
    }
    return DNSRecord(name_s.value(), type, c, ttl, soa.value());
  }
  default:
    // Unknown answer type
    content.remove_suffix(content.size() - rdlen);
    return DNSRecord(name_s.value(), type, c, ttl, std::string(content));
  }
}

DNSRecord::DNSRecord(const DNSName& name, DNSRecordType t, DNSRecordClass c,
  absl::Duration ttl, const IPAddress& addr) : name_(name), type_(t), class_(c),
  ttl_(ttl), expiry_(absl::Now() + ttl), value_(addr) {
  // Nothing to do.
}

DNSRecord::DNSRecord(const DNSName& name, DNSRecordType t, DNSRecordClass c,
  absl::Duration ttl, const DNSName& content) : name_(name), type_(t),
  class_(c), ttl_(ttl), expiry_(absl::Now() + ttl), value_(content) {
  // Nothing to do.
}

DNSRecord::DNSRecord(const DNSName& name, DNSRecordType t, DNSRecordClass c,
  absl::Duration ttl, const DNSMailExchange& mx) : name_(name), type_(t),
  class_(c), ttl_(ttl), expiry_(absl::Now() + ttl), value_(mx) {
  // Nothing to do.
}

DNSRecord::DNSRecord(const DNSName& name, DNSRecordType t, DNSRecordClass c,
  absl::Duration ttl, const DNSStartOfAuthority& soa) : name_(name), type_(t),
  class_(c), ttl_(ttl), expiry_(absl::Now() + ttl), value_(soa) {
  // Nothing to do.
}

DNSRecord::DNSRecord(const DNSName& name, DNSRecordType t, DNSRecordClass c,
  absl::Duration ttl, absl::string_view txt) : name_(name), type_(t), class_(c),
  ttl_(ttl), expiry_(absl::Now() + ttl), value_(txt) {
  // Nothing to do.
}

absl::Status DNSRecord::Serialize(std::ostringstream* os,
  absl::flat_hash_map<std::string, int>* comp_map) const {
  name_.Serialize(os, comp_map);

  uint8_t type_hi = ((unsigned int)type_) >> 8;
  uint8_t type_lo = ((unsigned int)type_) & 0xFF;
  uint8_t class_hi = ((unsigned int)class_) >> 8;
  uint8_t class_lo = ((unsigned int)class_) & 0xFF;
  uint32_t ttl = absl::ToInt64Seconds(ttl_);
  uint8_t ttl_1 = (ttl >> 24);
  uint8_t ttl_2 = (ttl >> 16) & 0xFF;
  uint8_t ttl_3 = (ttl >>  8) & 0xFF;
  uint8_t ttl_4 =  ttl        & 0xFF;
  
  *os << type_hi << type_lo << class_hi << class_lo << ttl_1 << ttl_2 << ttl_3;
  *os << ttl_4;
 
  size_t rdlen_ptr = os->str().size();

  *os << '\x00' << '\x00';

  size_t len_before = os->str().size();

  switch (type_) {
  case DNSRecordType::kA:
  case DNSRecordType::kAAAA: {
    if (!std::holds_alternative<IPAddress>(value_)) {
      return absl::InternalError(
        "address type but answer is not an IP address"
      );
    }
    IPAddress addr = std::get<IPAddress>(value_);
    *os << addr.AddressAsBytes();
    break;
  }
  case DNSRecordType::kCNAME:
  case DNSRecordType::kNS:
  case DNSRecordType::kPTR: {
    if (!std::holds_alternative<DNSName>(value_)) {
      return absl::InternalError(
        "domain type but answer is not a DNSName"
      );
    }
    DNSName name = std::get<DNSName>(value_);
    name.Serialize(os, comp_map);
    break;
  }
  case DNSRecordType::kMX: {
    if (!std::holds_alternative<DNSMailExchange>(value_)) {
      return absl::InternalError(
        "mx type but answer is not a DNSMailExchange"
      );
    }
    DNSMailExchange mx = std::get<DNSMailExchange>(value_);
    mx.Serialize(os, comp_map);
    break;
  }
  case DNSRecordType::kSOA: {
    if (!std::holds_alternative<DNSStartOfAuthority>(value_)) {
      return absl::InternalError(
        "mx type but answer is not a DNSMailExchange"
      );
    }
    DNSStartOfAuthority soa = std::get<DNSStartOfAuthority>(value_);
    soa.Serialize(os, comp_map);
    break;
  }
  case DNSRecordType::kTXT: {
    if (!std::holds_alternative<std::string>(value_)) {
      return absl::InternalError(
        "txt type but answer is not a string"
      );
    }
    std::string txt = std::get<std::string>(value_);
    *os << txt;
    break;
  }
  default:
    return absl::UnimplementedError("don't know how to serialize record type");
  }

  size_t len_after = os->str().size();
  uint16_t rdlen = len_after - len_before;

  std::string pkt(os->str());
  pkt[rdlen_ptr] = rdlen >> 8;
  pkt[rdlen_ptr + 1] = rdlen & 0xFF;
  os->str(pkt);

  return absl::OkStatus();
}

std::ostream& operator<<(std::ostream& os, const DNSRecord& ans) {
  os << ans.Name();
  os << ": type " << ans.Type() << ", class " << ans.Class() << ", ";
  bool is_addr = std::holds_alternative<IPAddress>(ans.value_);
  bool is_name = std::holds_alternative<DNSName>(ans.value_);
  bool is_mx = std::holds_alternative<DNSMailExchange>(ans.value_);
  bool is_soa = std::holds_alternative<DNSStartOfAuthority>(ans.value_);
  bool is_txt = std::holds_alternative<std::string>(ans.value_);
  std::string err("<record type and internal value type mismatch>");

  switch (ans.type_) {
  case DNSRecordType::kA:
  case DNSRecordType::kAAAA:
    if (!is_addr) return os << err;
    os << "addr " << std::get<IPAddress>(ans.value_);
    break;
  case DNSRecordType::kNS:
    if (!is_name) return os << err;
    os << "ns " << std::get<DNSName>(ans.value_);
    break;
  case DNSRecordType::kCNAME:
    if (!is_name) return os << err;
    os << "cname " << std::get<DNSName>(ans.value_);
    break;
  case DNSRecordType::kPTR:
    if (!is_name) return os << err;
    os << "ptr " << std::get<DNSName>(ans.value_);
    break;
  case DNSRecordType::kMX:
    if (!is_mx) return os << err;
    os << "mx " << std::get<DNSMailExchange>(ans.value_);
    break;
  case DNSRecordType::kSOA:
    if (!is_soa) return os << err;
    os << "soa " << std::get<DNSStartOfAuthority>(ans.value_);
    break;
  case DNSRecordType::kTXT:
    if (!is_txt) return os << err;
    os << "txt " << std::get<std::string>(ans.value_);
    break;
  default:
    os << "unknown content";
  }

  return os;
}

const DNSName& DNSRecord::Name() const {
  return name_;
}

void DNSRecord::Name(const DNSName& name) {
  name_ = name;
}

DNSRecordType DNSRecord::Type() const {
  return type_;
}

void DNSRecord::Type(DNSRecordType t) {
  type_ = t;
}

DNSRecordClass DNSRecord::Class() const {
  return class_;
}

void DNSRecord::Class(DNSRecordClass c) {
  class_ = c;
}

absl::Duration DNSRecord::TTL() const {
  return ttl_;
}

void DNSRecord::TTL(absl::Duration ttl) {
  ttl_ = ttl;
  expiry_ = absl::Now() + ttl_;
}

absl::StatusOr<const IPAddress> DNSRecord::ValueAsIP() const {
  try {
    return std::get<IPAddress>(value_);
  } catch(...) {
    return absl::FailedPreconditionError("not holding an IP address");
  }
}

absl::StatusOr<const DNSName> DNSRecord::ValueAsDomain() const {
  try {
    return std::get<DNSName>(value_);
  } catch(...) {
    return absl::FailedPreconditionError("not holding a DNSName");
  }
}

absl::StatusOr<const DNSMailExchange> DNSRecord::ValueAsMailExchange() const {
  try {
    return std::get<DNSMailExchange>(value_);
  } catch(...) {
    return absl::FailedPreconditionError("not holding a DNSMailExchange");
  }
}

absl::StatusOr<const DNSStartOfAuthority> DNSRecord::ValueAsStartOfAuthority()
  const {
  try {
    return std::get<DNSStartOfAuthority>(value_);
  } catch(...) {
    return absl::FailedPreconditionError("not holding a DNSStartOfAuthority");
  }
}

absl::StatusOr<const std::string> DNSRecord::ValueAsText() const {
  try {
    return std::get<std::string>(value_);
  } catch(...) {
    return absl::FailedPreconditionError("not holding a string");
  }
}

bool DNSRecord::HasExpired() const {
  return expiry_ < absl::Now();
}

DNSMessage::DNSMessage() : DNSMessage(0) {
  // Nothing to do.
}

DNSMessage::DNSMessage(uint16_t id) :
  id_(id),
  is_response_(false),
  authoritative_(false),
  truncated_(false),
  recursion_desired_(false),
  recursion_available_(false),
  authentic_data_(false),
  checking_disabled_(false),
  opcode_(DNSOpCode::kStandardQuery),
  rcode_(DNSRCode::kNoError) {
  // Nothing to do.
}

absl::StatusOr<std::string> DNSMessage::Serialize() const {
  // DNS Wire Format (in bytes)
  //
  //    0     1     2     3
  // +-----+-----+-----+-----+
  // | Query ID  |   Flags   |
  // +-----+-----+-----+-----+
  // |    # Qs   |   # Ans   |
  // +-----+-----+-----+-----+
  // |  # Auths  | # Addl RR |
  // +-----+-----+-----+-----+
  //
  // Serialize Questions, Answers, Authorities, and Additional Records
  // immediately after, in that order.
  std::ostringstream os;  // Stream we'll output the packet to
  // Compression map used to compress repeated domain names.
  absl::flat_hash_map<std::string, int> comp_map;
  // A value used when serializing 2-byte values
  uint16_t u16_data;

  u16_data = htons(id_);
  os << std::string((const char*)&u16_data, sizeof(u16_data));

  // Flag order:
  // - 1b Query/Response
  // - 4b OpCode
  // - 1b Authoritative Answer
  // - 1b Truncation Detected
  // - 1b Recursion Desired
  // - 1b Recursion Available
  // - 1b Zero (reserved, must be zero)
  // - 1b Authentic Data
  // - 1b Checking Disabled
  // - 4b RCode
  u16_data = htons((((uint16_t)is_response_) << 15) |
                   ((((uint16_t)opcode_) & 0xF) << 11) |
                   (((uint16_t)authoritative_) << 10) |
                   (((uint16_t)truncated_) << 9) |
                   (((uint16_t)recursion_desired_) << 8) |
                   (((uint16_t)recursion_available_) << 7) |
                   (((uint16_t)authentic_data_) << 5) |
                   (((uint16_t)checking_disabled_) << 4) |
                   (((uint16_t)rcode_) & 0xF));
  os << std::string((const char*)&u16_data, sizeof(u16_data));

  u16_data = htons((uint16_t)questions_.size());
  os << std::string((const char*)&u16_data, sizeof(u16_data));
  u16_data = htons((uint16_t)answers_.size());
  os << std::string((const char*)&u16_data, sizeof(u16_data));
  u16_data = htons((uint16_t)authorities_.size());
  os << std::string((const char*)&u16_data, sizeof(u16_data));
  u16_data = htons((uint16_t)additional_records_.size());
  os << std::string((const char*)&u16_data, sizeof(u16_data));

  for (const auto& it : questions_) {
    it.Serialize(&os, &comp_map);
  }

  for (const auto& it : answers_) {
    absl::Status s = it.Serialize(&os, &comp_map);
    if (!s.ok()) {
      return s;
    }
  }

  for (const auto& it : authorities_) {
    absl::Status s = it.Serialize(&os, &comp_map);
    if (!s.ok()) {
      return s;
    }
  }

  for (const auto& it : additional_records_) {
    absl::Status s = it.Serialize(&os, &comp_map);
    if (!s.ok()) {
      return s;
    }
  }

  return os.str();
}

absl::StatusOr<DNSMessage> DNSMessage::Deserialize(absl::string_view pkt) {
  DNSMessage msg;
  absl::string_view s(pkt);
  if (pkt.size() < 12) {
    return absl::InvalidArgumentError("truncated packet");
  }

  uint16_t u16_data;

#define READ_U16(x) \
  u16_data = ((uint16_t)static_cast<uint8_t>(pkt[(x)]) << 8) | \
             ((uint16_t)static_cast<uint8_t>(pkt[(x) + 1]) & 0xFF)
  
  msg.ID(READ_U16(0));

  // Read flags
  READ_U16(2);
  if (u16_data & 0x8000) {
    msg.UseAsResponse();
  } else {
    msg.UseAsQuery();
  }

  msg.OpCode((DNSOpCode)((u16_data >> 11) & 0xF));
  msg.Authoritative(u16_data & 0x0400);
  msg.Truncated(u16_data & 0x0200);
  msg.RecursionDesired(u16_data & 0x0100);
  msg.RecursionAvailable(u16_data & 0x0080);
  msg.AuthenticData(u16_data & 0x0020);
  msg.CheckingDisabled(u16_data & 0x0010);
  msg.RCode((DNSRCode)(u16_data & 0xF));

  uint16_t q_count = READ_U16(4);
  uint16_t a_count = READ_U16(6);
  uint16_t auth_count = READ_U16(8);
  uint16_t addl_count = READ_U16(10);
#undef READ_U16

  s.remove_prefix(12);
  for (uint16_t i = 0; i < q_count; ++i) {
    absl::StatusOr<DNSQuestion> q_s = DNSQuestion::Deserialize(&s, pkt);
    if (!q_s.ok()) {
      return q_s.status();
    }
    msg.MutableQuestions()->push_back(q_s.value());
  }

  for (uint16_t i = 0; i < a_count; ++i) {
    absl::StatusOr<DNSRecord> a_s = DNSRecord::Deserialize(&s, pkt);
    if (!a_s.ok()) {
      return a_s.status();
    }
    msg.MutableAnswers()->push_back(a_s.value());
  }

  for (uint16_t i = 0; i < auth_count; ++i) {
    absl::StatusOr<DNSRecord> auth_s = DNSRecord::Deserialize(&s, pkt);
    if (!auth_s.ok()) {
      return auth_s.status();
    }
    msg.MutableAuthorities()->push_back(auth_s.value());
  }

  for (uint16_t i = 0; i < addl_count; ++i) {
    absl::StatusOr<DNSRecord> addl_s = DNSRecord::Deserialize(&s, pkt);
    if (!addl_s.ok()) {
      return addl_s.status();
    }
    msg.MutableAdditionalRecords()->push_back(addl_s.value());
  }

  return msg;
}

uint16_t DNSMessage::ID() const {
  return id_;
}

void DNSMessage::ID(uint16_t id) {
  id_ = id;
}

bool DNSMessage::IsQuery() const {
  return !is_response_;
}

bool DNSMessage::IsResponse() const {
  return is_response_;
}

void DNSMessage::UseAsQuery() {
  is_response_ = false;
}

void DNSMessage::UseAsResponse() {
  is_response_ = true;
}

bool DNSMessage::Authoritative() const {
  return authoritative_;
}

void DNSMessage::Authoritative(bool authoritative) {
  authoritative_ = authoritative;
}

bool DNSMessage::Truncated() const {
  return truncated_;
}

void DNSMessage::Truncated(bool truncated) {
  truncated_ = truncated;
}

bool DNSMessage::RecursionDesired() const {
  return recursion_desired_;
}

void DNSMessage::RecursionDesired(bool recursion_desired) {
  recursion_desired_ = recursion_desired;
}

bool DNSMessage::RecursionAvailable() const {
  return recursion_available_;
}

void DNSMessage::RecursionAvailable(bool recursion_available) {
  recursion_available_ = recursion_available;
}

bool DNSMessage::AuthenticData() const {
  return authentic_data_;
}

void DNSMessage::AuthenticData(bool authentic_data) {
  authentic_data_ = authentic_data;
}

bool DNSMessage::CheckingDisabled() const {
  return checking_disabled_;
}

void DNSMessage::CheckingDisabled(bool checking_disabled) {
  checking_disabled_ = checking_disabled;
}

DNSOpCode DNSMessage::OpCode() const {
  return opcode_;
}

void DNSMessage::OpCode(DNSOpCode code) {
  opcode_ = code;
}

DNSRCode DNSMessage::RCode() const {
  return rcode_;
}

void DNSMessage::RCode(DNSRCode code) {
  rcode_ = code;
}

const std::vector<DNSQuestion>& DNSMessage::Questions() const {
  return questions_;
}

std::vector<DNSQuestion>* DNSMessage::MutableQuestions() {
  return &questions_;
}

const std::vector<DNSRecord>& DNSMessage::Answers() const {
  return answers_;
}

std::vector<DNSRecord>* DNSMessage::MutableAnswers() {
  return &answers_;
}

const std::vector<DNSRecord>& DNSMessage::Authorities() const {
  return authorities_;
}

std::vector<DNSRecord>* DNSMessage::MutableAuthorities() {
  return &authorities_;
}

const std::vector<DNSRecord>& DNSMessage::AdditionalRecords() const {
  return additional_records_;
}

std::vector<DNSRecord>* DNSMessage::MutableAdditionalRecords() {
  return &additional_records_;
}

NameResolver::NameResolver(Conduit* conduit) : conduit_(conduit),
  next_id_(0x241a), query_timeout_(absl::Seconds(10)),
  query_retransmit_interval_(absl::Milliseconds(500)) {
  // For now we just use Google's public DNS service.
  absl::StatusOr<IPAddress> resolver_s = IPAddress::From("8.8.8.8");
  if (!resolver_s.ok()) {
    // How? I don't know.
    return;
  }

  absl::StatusOr<std::shared_ptr<UDPSocket>> socket_s =
    UDPSocket::Connect(conduit, resolver_s.value(), 53);
  if (!socket_s.ok()) {
    // We failed to "connect" for some bizarre reason.
    return;
  }

  socket_ = socket_s.value();

  socket_->OnData([this](absl::string_view data) {
    HandleIncomingMessage(data);
  });

  socket_->MarkIdle();
}

absl::Duration NameResolver::QueryTimeout() const {
  return query_timeout_;
}

void NameResolver::QueryTimeout(absl::Duration qto) {
  query_timeout_ = qto;
}

absl::Duration NameResolver::QueryRetransmitInterval() const {
  return query_retransmit_interval_;
}

void NameResolver::QueryRetransmitInterval(absl::Duration qrti) {
  query_retransmit_interval_ = qrti;
}

std::shared_ptr<Promise<DNSMessage>> NameResolver::Query(DNSMessage* msg) {
  auto promise = std::make_shared<Promise<DNSMessage>>(conduit_,
    query_timeout_);

  msg->ID(next_id_);
  ++next_id_;
  absl::StatusOr<std::string> pkt_s = msg->Serialize();
  if (!pkt_s.ok()) {
    conduit_->OnNext([promise, pkt_s]() {
      promise->Reject(pkt_s.status());
    });
    return promise;
  }
  
  if (!socket_) {
    conduit_->OnNext([promise]() {
      promise->Reject(absl::InternalError("querying socket not available"));
    });
    return promise;
  }

  auto pkt = pkt_s.value();
  socket_->Write(pkt_s.value());
  auto retransmit_interval = conduit_->OnInterval(query_retransmit_interval_,
    [this, pkt](std::shared_ptr<Timer> self) {
    socket_->Write(pkt);
  });

  pending_queries_[msg->ID()] = promise;
  pending_retransmits_[msg->ID()] = retransmit_interval;
  socket_->MarkPending();

  auto id = msg->ID();
  promise->OnTimeout([this, id, retransmit_interval]() {
    // Operation Timeout
    conduit_->CancelTimer(retransmit_interval);
    pending_queries_.erase(id);
    pending_retransmits_.erase(id);
    if (pending_queries_.empty()) {
      socket_->MarkIdle();
    }
  });

  return promise;
}

std::shared_ptr<Promise<std::vector<DNSRecord>>> NameResolver::Lookup(
  absl::string_view name, DNSRecordType t, DNSRecordClass c) {
  auto promise = std::make_shared<Promise<std::vector<DNSRecord>>>();

  // Before querying a real server, we should probably check the cache.
  absl::StatusOr<const std::vector<DNSRecord>> cache_s =
    CacheLookup(name, t, c);

  if (cache_s.ok()) {
    // HIT!
    std::vector<DNSRecord> cache_data = cache_s.value();
    conduit_->OnNext([promise, cache_data]() {
      promise->Resolve(cache_data);
    });
    return promise;
  }

  // Cache MISS, send query
  DNSMessage msg;
  msg.UseAsQuery();
  msg.RecursionDesired(true);
  msg.MutableQuestions()->push_back(DNSQuestion(DNSName(name), t, c));

  auto query_promise = Query(&msg);
  query_promise->Then([this, promise](const DNSMessage& msg) {
    switch (msg.RCode()) {
    case DNSRCode::kNoError: {
      // Insert into cache
      std::vector<DNSRecord> records;
      auto answers = msg.Answers();
      auto authorities = msg.Authorities();
      auto addl = msg.AdditionalRecords();
      records.insert(records.end(), answers.begin(), answers.end());
      records.insert(records.end(), authorities.begin(), authorities.end());
      records.insert(records.end(), addl.begin(), addl.end());

      InsertCache(records);
      // Return to user
      promise->Resolve(records);
      break;
    }
    case DNSRCode::kNXDomain:
      promise->Reject(absl::NotFoundError("NXDOMAIN"));
      break;
    default:
      promise->Reject(absl::InternalError("unexpected rcode"));
    }
  });
  query_promise->Catch([promise](absl::Status err) {
    promise->Reject(err);
  });
  promise->DependsOnOther(query_promise);

  return promise;
}

std::shared_ptr<Promise<std::vector<IPAddress>>> NameResolver::Resolve(
  absl::string_view name) {
  auto promise = std::make_shared<Promise<std::vector<IPAddress>>>();

  // Make sure we don't have CNAMEs in the cache before attempting to check the
  // cache for A records.
  std::string domain(name);
  bool found_non_cname_domain = false;
  for (int i = 0; i < 10; ++i) {
    auto cache_s = CacheLookup(domain, DNSRecordType::kCNAME,
      DNSRecordClass::kIN);

    if (cache_s.ok()) {
      // If there are multiple CNAME records, take the first.
      DNSRecord record = cache_s.value()[0];

      absl::StatusOr<const DNSName> value_s = record.ValueAsDomain();
      if (!value_s.ok()) {
        conduit_->OnNext([promise]() {
          promise->Reject(absl::InternalError(
            "CNAME record with non-domain value"
          ));
        });
        return promise;
      }

      domain = value_s.value().Name();
    } else {
      found_non_cname_domain = true;
      break;
    }
  }

  if (!found_non_cname_domain) {
    conduit_->OnNext([promise]() {
      promise->Reject(absl::OutOfRangeError("CNAME loop detected"));
    });
    return promise;
  }
  
  // Does cache lookup for A records.
  auto lookup_promise = Lookup(name, DNSRecordType::kA, DNSRecordClass::kIN);
  lookup_promise->Then([promise](const std::vector<DNSRecord>& records) {
    std::vector<IPAddress> addresses;
    for (const auto& record : records) {
      if (record.Type() != DNSRecordType::kA) {
        continue;
      }

      auto ip_s = record.ValueAsIP();
      if (!ip_s.ok()) {
        promise->Reject(absl::InternalError("got A record with non-address"));
        return;
      }

      addresses.push_back(ip_s.value());
    }

    promise->Resolve(addresses);
  });
  lookup_promise->Catch([promise](absl::Status err) {
    promise->Reject(err);
  });
  promise->DependsOnOther(lookup_promise);

  return promise;
}

void NameResolver::PruneCache() {
  for (auto& domain : cache_) {
    PruneDomain(&(domain.second));

    if (domain.second.empty()) {
      cache_.erase(domain.first);
    }
  }
}

void NameResolver::FlushCache() {
  cache_.clear();
}

void NameResolver::HandleIncomingMessage(absl::string_view data) {
  // Attempt to get an ID before anything else.
  if (data.size() < 2) {
    return;
  }
  
  uint16_t id = (((uint16_t)static_cast<uint8_t>(data[0])) << 8) |
                            static_cast<uint8_t>(data[1]);
  if (!pending_queries_.contains(id)) {
    // This message wasn't intended for us.
    return;
  }

  auto promise = pending_queries_.at(id);

  absl::StatusOr<DNSMessage> msg_s = DNSMessage::Deserialize(data);
  
  pending_queries_.erase(id);
  if (pending_retransmits_.contains(id)) {
    conduit_->CancelTimer(pending_retransmits_.at(id));
    pending_retransmits_.erase(id);
  }

  if (pending_queries_.empty()) {
    socket_->MarkIdle();
  }

  if (!msg_s.ok()) {
    // Failed to parse response. Reject.
    promise->Reject(msg_s.status());
  } else {
    promise->Resolve(msg_s.value());
  }
}

void NameResolver::PruneDomain(
  absl::flat_hash_map<DNSRecordType, std::vector<DNSRecord>>* domain_cache) {
  for (auto& it : *domain_cache) {
    PruneDNSRecords(&(it.second));

    // All records of this type were pruned. We should prune this entry.
    if (it.second.empty()) {
      domain_cache->erase(it.first);
    }
  }
}

void NameResolver::PruneDNSRecords(std::vector<DNSRecord>* records) {
  for (size_t i = 0; i < records->size(); ++i) {
    if ((*records)[i].HasExpired()) {
      records->erase(records->begin() + i);
      --i;
    }
  }
}

absl::StatusOr<const std::vector<DNSRecord>> NameResolver::CacheLookup(
  absl::string_view name, DNSRecordType t, DNSRecordClass c) {
  auto domain =
    std::make_tuple<std::string, DNSRecordClass>(std::string(name),
      DNSRecordClass(c));

  if (!cache_.contains(domain)) {
    return absl::NotFoundError("domain/class not cached");
  }

  if (!cache_.at(domain).contains(t)) {
    return absl::NotFoundError("record type not cached");
  }

  PruneDNSRecords(&(cache_[domain][t]));

  if (cache_[domain][t].empty()) {
    cache_[domain].erase(t);
    if (cache_[domain].empty()) {
      cache_.erase(domain);
      return absl::NotFoundError("domain/class not cached");
    }

    return absl::NotFoundError("record type not cached");
  }

  return cache_[domain][t];
}

void NameResolver::InsertCache(const std::vector<DNSRecord>& records) {
  PruneCache();
  
  for (const auto& record : records) {
    auto domain_class = std::make_tuple<std::string, DNSRecordClass>(
      std::string(record.Name().Name()), DNSRecordClass(record.Class()));
    // Automatically creates it if it doesn't already exist.
    auto& domain_cache = cache_[domain_class];
    auto& type_cache = domain_cache[record.Type()];

    type_cache.push_back(record);
  }
}

}

