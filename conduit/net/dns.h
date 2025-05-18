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
// File: dns.h
// -----------------------------------------------------------------------------
//
// This file declares cd::NameResolver, which is an asynchronous DNS lookup
// facility.
//

#ifndef CONDUIT_NET_DNS_H_
#define CONDUIT_NET_DNS_H_

#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/time/time.h"

#include "conduit/conduit.h"
#include "conduit/net/net.h"
#include "conduit/net/socket.h"
#include "conduit/promise.h"

namespace cd {

// Describes a domain name
//
// Using this class simplifies three things:
// - Serialization into a packet
// - Deserialization from a packet
// - Compressing/uncompressing domain names
class DNSName {
public:
  // Not default-constructible
  DNSName() = delete;

  // Creates a domain name with the given dot-delimited domain.
  DNSName(absl::string_view name);

  const std::string& Name() const;
  void Name(absl::string_view name);

  // Outputs a serialized and compressed version of this domain name suitable
  // for use in DNS messages.
  //
  // For compression purposes, this method requires a hash map containing every
  // domain and subdomains, and their pointers into the rest of the message.
  void Serialize(std::ostringstream* os,
                 absl::flat_hash_map<std::string, int>* comp_map) const;

  // Deserializes the domain name that s starts off with, and does
  // remove_prefix on s to remove it from the "stream". The full message must
  // be provided as a lookup table for compression pointers.
  static absl::StatusOr<DNSName> Deserialize(absl::string_view* s,
                                             absl::string_view msg);

  friend std::ostream& operator<<(std::ostream& os, const DNSName& name);

private:
  std::string name_;
};

// Describes an MX record
class DNSMailExchange {
public:
  // Not default-constructible
  DNSMailExchange() = delete;

  // Creates an MX with the given priority and domain.
  DNSMailExchange(uint16_t priority, const DNSName& name);

  uint16_t Priority() const;
  void Priority(uint16_t priority);

  const DNSName& Name() const;
  void Name(const DNSName& name);

  // Outputs a serialized and compressed version of this MX suitable for use in
  // DNS messages.
  void Serialize(std::ostringstream* os,
                 absl::flat_hash_map<std::string, int>* comp_map) const;

  // Deserializes the MX RDATA that s starts off with.
  static absl::StatusOr<DNSMailExchange> Deserialize(absl::string_view* s,
                                                     absl::string_view msg);

  friend std::ostream& operator<<(std::ostream& os, const DNSMailExchange& mx);

private:
  uint16_t priority_;
  DNSName name_;
};

// Describes an SOA record
class DNSStartOfAuthority {
public:
  // Not default-constructible
  DNSStartOfAuthority() = delete;

  // Constructs a new SOA record with the Authoritative Nameserver, Contact
  // email address (@ is internally replaced with .), serial number, refresh
  // interval, retry delay, expiration of authority time, and default TTL for
  // records in the corresponding zone.
  DNSStartOfAuthority(const DNSName& auth_ns, absl::string_view email,
    uint32_t serial, absl::Duration refresh, absl::Duration retry,
    absl::Duration expire, absl::Duration default_ttl);

  const DNSName& AuthoritativeNS() const;
  void AuthoritativeNS(const DNSName& auth_ns);

  // Returns the email address as would appear over SMTP, not over DNS
  const std::string& Email() const;
  void Email(absl::string_view email);

  uint32_t Serial() const;
  void Serial(uint32_t serial);

  absl::Duration RefreshInterval() const;
  void RefreshInterval(absl::Duration refresh);

  absl::Duration RetryDelay() const;
  void RetryDelay(absl::Duration retry);

  absl::Duration ExpirationTime() const;
  void ExpirationTime(absl::Duration expire);

  absl::Duration DefaultTTL() const;
  void DefaultTTL(absl::Duration default_ttl);

  void Serialize(std::ostringstream* os,
                 absl::flat_hash_map<std::string, int>* comp_map) const;

  static absl::StatusOr<DNSStartOfAuthority> Deserialize(absl::string_view* s,
    absl::string_view msg);

  friend std::ostream& operator<<(std::ostream& os,
                                  const DNSStartOfAuthority& soa);

private:
  DNSName auth_ns_;
  std::string email_;
  uint32_t serial_;
  absl::Duration refresh_;
  absl::Duration retry_;
  absl::Duration expire_;
  absl::Duration default_ttl_;
};

// Describes types of DNS records, such as A, MX, and TXT
enum class DNSRecordType : uint16_t {
  kUnknown = 0,               // Description
                              // -----------------------------------------------
  kA = 1,                     // Host address
  kNS = 2,                    // Nameserver
  kCNAME = 5,                 // Canonical hostname
  kSOA = 6,                   // Start of authority
  kPTR = 12,                  // Reverse domain pointer
  kMX = 15,                   // Mail exchange
  kTXT = 16,                  // Plain text record
  kAAAA = 28,                 // IPv6 host address
  kLOC = 29,                  // Geographic location
  kSRV = 33,                  // Service locator
  kNAPTR = 35,                // Naming authority pointer
  kDS = 43,                   // DNSSEC delegation signer
  kIPSECKEY = 45,             // IPsec key
  kRRSIG = 46,                // DNSSEC signature
  kNSEC = 47,                 // Next secure record
  kDNSKEY = 48,               // DNSSEC key
  kNSEC3 = 50,                // Next secure record v3 extension
  kNSEC3PARAM = 51,           // Next secure record v3 parameter

  // QTYPEs, valid only in the Questions section of a message
  kAXFR = 252,                // Transfer entire zone
  kMAILB = 253,               // Mailbox related records
  kAll = 255,                 // All records requested
};

std::string StringifyDNSRecordType(const DNSRecordType& type);
std::ostream& operator<<(std::ostream& os, const DNSRecordType& t);

// Describes classes of DNS records. Really only class IN is supported.
enum class DNSRecordClass : uint16_t {
  kUnknown = 0,               // Description
                              // -----------------------------------------------
  kIN = 1,                    // Internet

  // QCLASSes, valid only in the Questions section of a message
  kAll = 255,                 // All classes requested
};

std::string StringifyDNSRecordClass(const DNSRecordClass& c);
std::ostream& operator<<(std::ostream& os, const DNSRecordClass& c);

// Represents a DNS question, which can be contained in a DNSMessage.
class DNSQuestion {
public:
  // Not default constructible
  DNSQuestion() = delete;

  // Creates a question by attempting to deserialize a string.
  //
  // NOTE: This method takes a string_view pointer, and uses remove_prefix to
  // remove the actual question from the string after being processed. If an
  // error is returned, the packet is in an unknown state, though errors should
  // be treated as fatal for the packet as a whole. For purposes of
  // decompression, it also needs a string_view of the entire received message.
  static absl::StatusOr<DNSQuestion> Deserialize(absl::string_view* pkt,
    absl::string_view msg);

  // Creates a question with the provided requirements
  DNSQuestion(const DNSName& name, DNSRecordType type,
              DNSRecordClass r_class = DNSRecordClass::kIN);

  void Serialize(std::ostringstream* os,
                 absl::flat_hash_map<std::string, int>* comp_map) const;
  
  // Writes a human-readable form of the question to the stream
  friend std::ostream& operator<<(std::ostream& os, const DNSQuestion& q);

  const DNSName& Name() const;
  void Name(const DNSName& name);

  DNSRecordType Type() const;
  void Type(DNSRecordType t);

  DNSRecordClass Class() const;
  void Class(DNSRecordClass c);

private:
  DNSName name_;          // e.g. www.google.com
  DNSRecordType type_;    // e.g. A
  DNSRecordClass class_;  // e.g. IN
};

// Represents a DNS answer, aka a Resource Record.
class DNSRecord {
public:
  using ValueT = std::variant<
    IPAddress,
    DNSName,
    DNSMailExchange,
    DNSStartOfAuthority,
    std::string
  >;

  // Not default-constructible
  DNSRecord() = delete;

  // Creates an answer by attempting to deserialize a string.
  //
  // NOTE: Like with DNSQuestion, this method take a string_view pointer, and
  // uses remove_prefix to remove the actual question from the string after
  // being processed. If an error is returned, the packet is in an unknown
  // state, though errors should be treated as fatal for the packet as a whole.
  static absl::StatusOr<DNSRecord> Deserialize(absl::string_view* pkt,
                                               absl::string_view msg);

  // Creates a DNS resource record with an IP address.
  DNSRecord(const DNSName& name,
            DNSRecordType t,
            DNSRecordClass c,
            absl::Duration ttl,
            const IPAddress& addr);

  // Creates a DNS resource record with a domain name
  DNSRecord(const DNSName& name,
            DNSRecordType t,
            DNSRecordClass c,
            absl::Duration ttl,
            const DNSName& content);

  // Creates a DNS resource record with a mail exchange
  DNSRecord(const DNSName& name,
            DNSRecordType t,
            DNSRecordClass c,
            absl::Duration ttl,
            const DNSMailExchange& mx);

  // Creates a DNS resource record with a start of authority (SOA)
  DNSRecord(const DNSName& name,
            DNSRecordType t,
            DNSRecordClass c,
            absl::Duration ttl,
            const DNSStartOfAuthority& soa);

  // Creates a DNS resource record with plain text
  //
  // The plain text could be truly plain (TXT), or some other piece of info that
  // didn't have its own representation.
  DNSRecord(const DNSName& name,
            DNSRecordType t,
            DNSRecordClass c,
            absl::Duration ttl,
            absl::string_view txt);

  // Will fail if the record type doesn't correspond to the actual type of
  // value stored in the DNSRecord.
  absl::Status Serialize(std::ostringstream* os,
                         absl::flat_hash_map<std::string, int>* comp_map) const;

  // Writes a human-readable form of this DNS answer.
  friend std::ostream& operator<<(std::ostream& os, const DNSRecord& ans);

  const DNSName& Name() const;
  void Name(const DNSName& name);

  DNSRecordType Type() const;
  void Type(DNSRecordType t);

  DNSRecordClass Class() const;
  void Class(DNSRecordClass c);

  // Retrieves and sets the TTL of this DNSRecord
  //
  // Side effect: also sets an internal expiry time that is used to check for
  // cache expiry in HasExpired().
  absl::Duration TTL() const;
  void TTL(absl::Duration ttl);

  absl::StatusOr<const IPAddress> ValueAsIP() const;
  absl::StatusOr<const DNSName> ValueAsDomain() const;
  absl::StatusOr<const DNSMailExchange> ValueAsMailExchange() const;
  absl::StatusOr<const DNSStartOfAuthority> ValueAsStartOfAuthority() const;
  absl::StatusOr<const std::string> ValueAsText() const;

  // Checks if this DNSRecord has expired based on the TTL and the time the TTL
  // was set.
  bool HasExpired() const;

private:
  DNSName name_;
  DNSRecordType type_;
  DNSRecordClass class_;
  absl::Duration ttl_;
  absl::Time expiry_;
  ValueT value_;
};

// Represents an operation code for a DNS message
enum class DNSOpCode {
                                 // Description
                                 // --------------------------------------------
  kStandardQuery = 0,            // Standard query
  kStatus = 2,                   // Server status request
  kNotify = 4,                   // Notification of zone changes
  kUpdate = 5,                   // Dynamic update
};

// Represents a response code for a DNS message
enum class DNSRCode {
                                 // Description
                                 // --------------------------------------------
  kNoError = 0,                  // OK
  kFormErr = 1,                  // Format error
  kServFail = 2,                 // Server failure
  kNXDomain = 3,                 // Non-existent domain
  kNotImp = 4,                   // Not implemented
  kRefused = 5,                  // Query refused
  kYXDomain = 6,                 // Name exists when it should not
  kYXRRSet = 7,                  // RR exists when it should not
  kNXRRSet = 8,                  // RR does not exist when it should
  kNotAuth = 9,                  // Not authorized/server not authoritative
  kNotZone = 10,                 // Name not contained in zone
};

// Represents a DNS message unit that can be converted to and from a packet.
class DNSMessage {
public:
  // Constructs a basic DNS query with no questions and all flags set to 0. The
  // constructor without an ID just sets the message ID to 0x0000.
  DNSMessage();
  DNSMessage(uint16_t id);

  // Serializes the message into a format suitable for transmission over the
  // wire.
  absl::StatusOr<std::string> Serialize() const;

  // Deserializes a message from a transmission over the wire.
  static absl::StatusOr<DNSMessage> Deserialize(absl::string_view pkt);
  
  uint16_t ID() const;
  void ID(uint16_t id);

  // Checks if this message is a query/response
  bool IsQuery() const;
  bool IsResponse() const;
  // Changes what type of message this is
  void UseAsQuery();
  void UseAsResponse();

  bool Authoritative() const;
  void Authoritative(bool authoritative);

  bool Truncated() const;
  void Truncated(bool truncated);

  bool RecursionDesired() const;
  void RecursionDesired(bool recursion_desired);

  bool RecursionAvailable() const;
  void RecursionAvailable(bool recursion_available);

  bool AuthenticData() const;
  void AuthenticData(bool authentic_data);

  bool CheckingDisabled() const;
  void CheckingDisabled(bool checking_disabled);

  DNSOpCode OpCode() const;
  void OpCode(DNSOpCode code);

  DNSRCode RCode() const;
  void RCode(DNSRCode code);

  const std::vector<DNSQuestion>& Questions() const;
  std::vector<DNSQuestion>* MutableQuestions();

  const std::vector<DNSRecord>& Answers() const;
  std::vector<DNSRecord>* MutableAnswers();

  const std::vector<DNSRecord>& Authorities() const;
  std::vector<DNSRecord>* MutableAuthorities();

  const std::vector<DNSRecord>& AdditionalRecords() const;
  std::vector<DNSRecord>* MutableAdditionalRecords();

private:
  uint16_t id_;
  bool is_response_ : 1;
  bool authoritative_ : 1;
  bool truncated_ : 1;
  bool recursion_desired_ : 1;
  bool recursion_available_ : 1;
  bool authentic_data_ : 1;
  bool checking_disabled_ : 1;
  DNSOpCode opcode_;
  DNSRCode rcode_;
  std::vector<DNSQuestion> questions_;
  std::vector<DNSRecord> answers_;
  std::vector<DNSRecord> authorities_;
  std::vector<DNSRecord> additional_records_;
};

// Represents a generalized, caching DNS query agent.
//
// The recommended use of this class is to create exactly one instance of it for
// the entire process, and using one of Query(), Lookup(), or Resolve() to
// retrieve data. The differences are described below.
//
// Query(): allows you to choose the exact query contents as they appear on the
// wire.
//
// Lookup(): a wrapper around Query() that allows for caching.
//
// Resolve(): a wrapper around Lookup() that handles CNAME records and just
// completely hides all complexities of DNS from the user, only returning a list
// of IP addresses to attempt connections to.
//
// NameResolver is also configurable in code *only*. The purpose of this is to
// attempt to make the execution environment as little of a factor as possible
// in the operation of foundational facilities like this one. The user can
// (although is discouraged from doing so) parse a system-wide configuration
// like /etc/resolv.conf and load values into these options.
//
// Options:
// - QueryTimeout: the amount of time the agent should wait for a response
//   before timing out. (default: 10s)
// - QueryRetransmitInterval: The amount of time the agent should wait for a
//   response before attempting to retransmit the query, presuming packet loss.
//   (default: 500ms)
// - UseNameServer: adds the IP address of a DNS server to the list of name
//   servers that this NameResolver queries. By default, the first name server
//   is queried, and subsequent ones only if the first one does not respond
//   within the retransmission interval.
// - RoundRobin: tells the NameResolver whether or not to use a round-robin
//   approach to selecting which name servers to query. In this mode, the name
//   server used for a query is the least recently used one. Queries that fail
//   to yield responses within retransmission intervals get sent to the next one
//   in the list.
class NameResolver {
public:
  // Not default-constructible
  NameResolver() = delete;
  
  // Creates a new NameResolver with network capabilities.
  NameResolver(Conduit* conduit);

  // Options
  absl::Duration QueryTimeout() const;
  void QueryTimeout(absl::Duration qto);

  absl::Duration QueryRetransmitInterval() const;
  void QueryRetransmitInterval(absl::Duration qrti);
  
  void UseNameServer(IPAddress addr);

  bool RoundRobin() const;
  void RoundRobin(bool rr);

  // Initiates a DNS query.
  //
  // Side effect: `query` is altered to contain the next message ID in this
  // name resolver's sequence.
  std::shared_ptr<Promise<DNSMessage>> Query(DNSMessage* query);

  // Finds records for a domain.
  //
  // The difference between Lookup() and Query() is that Lookup() may use
  // caching for faster response times.
  std::shared_ptr<Promise<std::vector<DNSRecord>>> Lookup(
    absl::string_view name, DNSRecordType t,
    DNSRecordClass c = DNSRecordClass::kIN);

  // Performs a Lookup() to get an IP address for the given domain.
  std::shared_ptr<Promise<std::vector<IPAddress>>> Resolve(
    absl::string_view name);

  // Purges expired DNS records from the internal in-memory cache.
  void PruneCache();

  // Clears the internal cache, requiring all new lookups to re-query the
  // server.
  void FlushCache();

private:
  void HandleIncomingMessage(absl::string_view data);

  // Performs the actions of PruneCache on a specific domain/class pair in the
  // cache.
  void PruneDomain(
    absl::flat_hash_map<DNSRecordType, std::vector<DNSRecord>>* domain_cache);

  // Performs the actions of PruneCache on a specific vector of records.
  void PruneDNSRecords(std::vector<DNSRecord>* records);

  // Performs a lookup only within the internal cache.
  //
  // Has the same signature as Lookup(), with the difference being return value.
  // This method resolves instantly. If there was a cache miss, and instance of
  // absl::NotFoundError is returned. This method also lazy-prunes all expired
  // records that it discovers.
  absl::StatusOr<const std::vector<DNSRecord>> CacheLookup(
    absl::string_view name, DNSRecordType t,
    DNSRecordClass c = DNSRecordClass::kIN);

  // Inserts records into the cache, and calls PruneCache.
  void InsertCache(const std::vector<DNSRecord>& records);

  Conduit* conduit_;
  std::shared_ptr<UDPSocket> socket_;
  uint16_t next_id_;

  // A map of query IDs to promises/retransmission intervals
  absl::flat_hash_map<uint16_t,
                      std::shared_ptr<Promise<DNSMessage>>>
    pending_queries_;
  absl::flat_hash_map<uint16_t,
                      std::shared_ptr<Timer>> pending_retransmits_;

  // DNS cache for this resolver
  //
  // Structure example:
  // - localhost, IN
  //   - A: {127.0.0.1}
  //   - AAAA: {[::1]}
  // - www.example.com, IN
  //   - CNAME: {example.com}
  // - example.com, IN
  //   - A: {1.2.3.4}
  absl::flat_hash_map<std::tuple<std::string, DNSRecordClass>,
                      absl::flat_hash_map<DNSRecordType,
                                          std::vector<DNSRecord>>> cache_;

  // Options
  absl::Duration query_timeout_;
  absl::Duration query_retransmit_interval_;
};

}

#endif  // CONDUIT_NET_DNS_H_

