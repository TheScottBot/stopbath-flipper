/*
 * Fuzz harness for the protocol parser, the one untrusted input surface in
 * the application (specification 0.10 guidance, FE3).
 *
 * Two ways to run it:
 *
 * - Under libFuzzer, when built with clang and -fsanitize=fuzzer, through
 *   LLVMFuzzerTestOneInput. Define REMOTE_FUZZ_LIBFUZZER for that build.
 * - As an ordinary program (the default build), which drives the same entry
 *   point from a deterministic pseudo random generator: random byte streams,
 *   and mutations of lines the encoder produced. The sequence is fixed by the
 *   seed, so a failure reproduces. This is what continuous integration runs,
 *   under the sanitisers, so no clang is needed anywhere.
 *
 * The oracle: every message the parser accepts is re-encoded and re-parsed,
 * and must come back identical. Any memory fault is the sanitiser's to find.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../protocol/remote_protocol.h"

static void fuzz_check(bool condition, const char* description) {
    if(!condition) {
        fprintf(stderr, "fuzz oracle failed: %s\n", description);
        abort();
    }
}

/* Outcome counts, so a run can show it reached accepted messages and every
 * refusal, rather than only proving nothing crashed. */
static uint32_t accepted_messages = 0;
static uint32_t refusals_by_error[RemoteProtocolErrorCount];

/* Feeds one input through the assembler and applies the oracle to every
 * message that comes out. */
static void exercise(const uint8_t* input, size_t input_length) {
    static RemoteProtocolLineAssembler assembler;
    static RemoteProtocolMessage message;
    static RemoteProtocolMessage reparsed;
    static char line[REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH];

    remote_protocol_line_assembler_initialise(&assembler);
    for(size_t index = 0; index < input_length; index++) {
        RemoteProtocolFeedOutcome outcome = remote_protocol_feed_byte(&assembler, input[index], &message);
        if(outcome.kind == RemoteProtocolFeedOutcomeError) {
            fuzz_check(outcome.error < RemoteProtocolErrorCount, "error code within the set");
            refusals_by_error[outcome.error]++;
        }
        if(outcome.kind != RemoteProtocolFeedOutcomeMessage) {
            continue;
        }
        accepted_messages++;
        size_t line_length = 0;
        fuzz_check(remote_protocol_encode(&message, line, sizeof(line), &line_length), "an accepted message re-encodes");
        fuzz_check(line_length > 0 && line_length <= REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH, "re-encoded length within the bound");
        RemoteProtocolFeedOutcome again = remote_protocol_parse_line(line, line_length - 1, &reparsed);
        fuzz_check(again.kind == RemoteProtocolFeedOutcomeMessage, "a re-encoded message re-parses");
        fuzz_check(reparsed.verb == message.verb, "same verb after the round trip");
        const RemoteProtocolVerbDescriptor* verb = remote_protocol_verb_descriptor(message.verb);
        for(int field_index = 0; field_index < verb->field_count; field_index++) {
            fuzz_check(reparsed.fields[field_index].integer == message.fields[field_index].integer, "same integer after the round trip");
            fuzz_check(reparsed.fields[field_index].text_length == message.fields[field_index].text_length, "same text length after the round trip");
            fuzz_check(memcmp(reparsed.fields[field_index].text, message.fields[field_index].text, message.fields[field_index].text_length) == 0, "same text after the round trip");
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* input, size_t input_length);
int LLVMFuzzerTestOneInput(const uint8_t* input, size_t input_length) {
    exercise(input, input_length);
    return 0;
}

#ifndef REMOTE_FUZZ_LIBFUZZER

/* xorshift32: small, fast, and deterministic, which is all a reproducible
 * driver needs. Not a source of randomness for anything else. */
static uint32_t random_state = 0x2545F491u;

static uint32_t next_random(void) {
    uint32_t value = random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    random_state = value;
    return value;
}

static uint32_t random_below(uint32_t bound) {
    return bound == 0 ? 0 : next_random() % bound;
}

/* Builds a valid message of a random verb with random field values, so the
 * mutation cases start from lines the parser accepts. */
static size_t random_valid_line(char* line, size_t capacity) {
    static const char* const text_samples[] = {
        "", "HTTP://192.168.72.1/", "WIFI:T:WPA;S:a b;P:x%y=z;;", "~-._", "flipper-zero", "a"};
    RemoteProtocolMessage message;
    RemoteProtocolVerb verb = (RemoteProtocolVerb)random_below(RemoteProtocolVerbCount);
    remote_protocol_message_initialise(&message, verb);
    const RemoteProtocolVerbDescriptor* descriptor = remote_protocol_verb_descriptor(verb);
    for(int field_index = 0; field_index < descriptor->field_count; field_index++) {
        const RemoteProtocolFieldDescriptor* field = &descriptor->fields[field_index];
        switch(field->type) {
        case RemoteProtocolFieldTypeInteger:
            remote_protocol_message_set_integer(&message, field_index, field->minimum + random_below(field->maximum - field->minimum + 1));
            break;
        case RemoteProtocolFieldTypeBoolean:
            remote_protocol_message_set_integer(&message, field_index, random_below(2));
            break;
        case RemoteProtocolFieldTypeEnumeration: {
            int value_count = 0;
            remote_protocol_enumeration_values(field->enumeration, &value_count);
            remote_protocol_message_set_integer(&message, field_index, random_below((uint32_t)value_count));
            break;
        }
        case RemoteProtocolFieldTypeToken:
            remote_protocol_message_set_text(&message, field_index, random_below(2) ? "flipper-zero" : "a");
            break;
        case RemoteProtocolFieldTypeText:
            remote_protocol_message_set_text(&message, field_index, text_samples[random_below(sizeof(text_samples) / sizeof(text_samples[0]))]);
            break;
        }
    }
    size_t line_length = 0;
    if(!remote_protocol_encode(&message, line, capacity, &line_length)) {
        return 0;
    }
    return line_length;
}

#define INPUT_CAPACITY (REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH * 3)

static size_t make_input(uint8_t* input) {
    uint32_t strategy = random_below(4);
    if(strategy == 0) {
        /* Pure noise, mostly printable so it gets past the first checks. The
         * line feed density varies so that some runs pass the message bound
         * without a terminator, which is the TOO_LONG path. */
        size_t length = random_below(INPUT_CAPACITY);
        uint32_t newline_per_thousand = random_below(3) == 0 ? 1 : 50;
        for(size_t index = 0; index < length; index++) {
            uint32_t roll = random_below(1000);
            if(roll < newline_per_thousand) input[index] = '\n';
            else if(roll < newline_per_thousand + 50) input[index] = (uint8_t)random_below(256);
            else input[index] = (uint8_t)(0x20 + random_below(0x5F));
        }
        return length;
    }
    /* One to three valid lines, then mutated. */
    size_t length = 0;
    uint32_t line_count = 1 + random_below(3);
    for(uint32_t line_index = 0; line_index < line_count; line_index++) {
        char line[REMOTE_PROTOCOL_MAXIMUM_MESSAGE_LENGTH];
        size_t line_length = random_valid_line(line, sizeof(line));
        if(length + line_length > INPUT_CAPACITY) break;
        memcpy(input + length, line, line_length);
        length += line_length;
    }
    if(strategy == 1 || length == 0) return length;
    uint32_t mutation_count = 1 + random_below(4);
    for(uint32_t mutation = 0; mutation < mutation_count; mutation++) {
        uint32_t kind = random_below(4);
        size_t position = random_below((uint32_t)length);
        if(kind == 0) {
            input[position] = (uint8_t)random_below(256);
        } else if(kind == 1 && length + 1 < INPUT_CAPACITY) {
            memmove(input + position + 1, input + position, length - position);
            input[position] = (uint8_t)(0x20 + random_below(0x5F));
            length++;
        } else if(kind == 2 && length > 1) {
            memmove(input + position, input + position + 1, length - position - 1);
            length -= 1;
        } else {
            /* Truncate: the line loses its end, or its terminator. */
            length = position;
        }
    }
    return length;
}

int main(int argument_count, char** arguments) {
    uint32_t iterations = 200000;
    if(argument_count > 1) {
        iterations = (uint32_t)strtoul(arguments[1], NULL, 10);
    }
    if(argument_count > 2) {
        random_state = (uint32_t)strtoul(arguments[2], NULL, 10);
        if(random_state == 0) random_state = 1;
    }
    static uint8_t input[INPUT_CAPACITY];
    for(uint32_t iteration = 0; iteration < iterations; iteration++) {
        size_t length = make_input(input);
        exercise(input, length);
    }
    printf("fuzz: %u inputs, no oracle failure; %u messages accepted\n", iterations, accepted_messages);
    int error_count = 0;
    const char* const* error_names = remote_protocol_enumeration_values(RemoteProtocolEnumerationError, &error_count);
    for(int error_index = 0; error_index < error_count; error_index++) {
        if(refusals_by_error[error_index] > 0) {
            printf("  %s: %u\n", error_names[error_index], refusals_by_error[error_index]);
        }
    }
    return 0;
}

#endif
