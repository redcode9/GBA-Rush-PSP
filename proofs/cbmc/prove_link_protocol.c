#include <stdint.h>

#include "link_protocol.h"

extern size_t nondet_size(void);

static int parser_valid(const LinkProtocolParser *parser)
{
  if(parser->used > LINK_PROTOCOL_MAX_PACKET ||
     parser->expected > LINK_PROTOCOL_MAX_PACKET)
    return 0;
  if(parser->used < LINK_PROTOCOL_HEADER_SIZE)
    return parser->expected == 0;
  if(parser->expected == 0)
    return parser->used == LINK_PROTOCOL_HEADER_SIZE;
  return parser->expected >= LINK_PROTOCOL_HEADER_SIZE &&
   parser->used <= parser->expected;
}

int main(void)
{
  uint8_t input[LINK_PROTOCOL_MAX_PACKET + 1u];
  LinkPacket decoded;
  LinkPacket parsed;
  LinkProtocolError error;
  LinkProtocolParser parser;
  LinkParseResult parse_result;
  size_t consumed = 0;
  size_t input_size = nondet_size();

  __CPROVER_assume(input_size <= sizeof(input));
  error = link_protocol_decode(input, input_size, &decoded);
  __CPROVER_assert(error >= LINK_PROTOCOL_OK &&
   error <= LINK_PROTOCOL_ERROR_CRC, "decoder returns a declared result");
  if(error == LINK_PROTOCOL_OK)
  {
    __CPROVER_assert(input_size >= LINK_PROTOCOL_HEADER_SIZE &&
     input_size <= LINK_PROTOCOL_MAX_PACKET,
     "accepted frame length is bounded");
    __CPROVER_assert(decoded.payload_size <= LINK_PROTOCOL_MAX_PAYLOAD,
     "accepted payload length is bounded");
    __CPROVER_assert(link_protocol_message_known(decoded.type) ||
     (decoded.flags & LINK_PROTOCOL_FLAG_OPTIONAL) != 0,
     "accepted message type is known or optional");
  }

  __CPROVER_assume(parser_valid(&parser));
  parse_result = link_protocol_parser_feed(&parser, input, input_size,
   &consumed, &parsed);
  __CPROVER_assert(parse_result >= LINK_PARSE_ERROR &&
   parse_result <= LINK_PARSE_PACKET, "parser returns a declared result");
  __CPROVER_assert(consumed <= input_size,
   "parser never consumes beyond the supplied buffer");
  __CPROVER_assert(parser_valid(&parser),
   "parser state remains bounded");
  if(parse_result == LINK_PARSE_PACKET)
    __CPROVER_assert(parsed.payload_size <= LINK_PROTOCOL_MAX_PAYLOAD,
     "stream parser output payload is bounded");

  return 0;
}
