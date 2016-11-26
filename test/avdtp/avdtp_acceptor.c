/*
 * Copyright (C) 2016 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MATTHIAS
 * RINGWALD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at 
 * contact@bluekitchen-gmbh.com
 *
 */


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "btstack.h"
#include "avdtp.h"
#include "avdtp_util.h"
#include "avdtp_acceptor.h"

static int avdtp_pack_service_capabilities(uint8_t * buffer, int size, avdtp_capabilities_t caps, avdtp_service_category_t category, uint8_t pack_all_capabilities){
    int i;
    // pos = 0 reserved for length
    int pos = 1;
    switch(category){
        case AVDTP_MEDIA_TRANSPORT:
        case AVDTP_REPORTING:
            break;
        case AVDTP_DELAY_REPORTING:
            if (!pack_all_capabilities) break;
            break;
        case AVDTP_RECOVERY:
            buffer[pos++] = caps.recovery.recovery_type; // 0x01=RFC2733
            buffer[pos++] = caps.recovery.maximum_recovery_window_size;
            buffer[pos++] = caps.recovery.maximum_number_media_packets;
            break;
        case AVDTP_CONTENT_PROTECTION:
            buffer[pos++] = caps.content_protection.cp_type_lsb;
            buffer[pos++] = caps.content_protection.cp_type_msb;
            // if (caps.content_protection.cp_type_value_len == 0){
            //     buffer[pos++] = 0;
            // }
            for (i = 0; i<caps.content_protection.cp_type_value_len; i++){
                buffer[pos++] = caps.content_protection.cp_type_value[i];
            }
            printf("AVDTP_CONTENT_PROTECTION %d, %d \n", caps.content_protection.cp_type_lsb, caps.content_protection.cp_type_msb);
            break;
        case AVDTP_HEADER_COMPRESSION:
            buffer[pos++] = (caps.header_compression.back_ch << 7) | (caps.header_compression.media << 6) | (caps.header_compression.recovery << 5);
            break;
        case AVDTP_MULTIPLEXING:
            buffer[pos++] = caps.multiplexing_mode.fragmentation << 7;
            for (i=0; i<caps.multiplexing_mode.transport_identifiers_num; i++){
                buffer[pos++] = caps.multiplexing_mode.transport_session_identifiers[i] << 7;
                buffer[pos++] = caps.multiplexing_mode.tcid[i] << 7;
                // media, reporting. recovery
            }
            break;
        case AVDTP_MEDIA_CODEC:
            buffer[pos++] = ((uint8_t)caps.media_codec.media_type) << 4;
            buffer[pos++] = (uint8_t)caps.media_codec.media_codec_type;
            for (i = 0; i<caps.media_codec.media_codec_information_len; i++){
                buffer[pos++] = caps.media_codec.media_codec_information[i];
            }
            break;
    }
    buffer[0] = pos - 1; // length
    return pos;
}


static uint16_t avdtp_unpack_service_capabilities(avdtp_capabilities_t * caps, uint8_t * packet, uint16_t size){
    int pos = 0;
    uint16_t registered_service_categories = 0;

    avdtp_service_category_t category = (avdtp_service_category_t)packet[pos++];
    uint8_t cap_len = packet[pos++];
    int i;
    while (pos < size){
        switch(category){
            case AVDTP_MEDIA_TRANSPORT:
            case AVDTP_REPORTING:
            case AVDTP_DELAY_REPORTING:
                pos++;
                break;
            case AVDTP_RECOVERY:
                caps->recovery.recovery_type = packet[pos++];
                caps->recovery.maximum_recovery_window_size = packet[pos++];
                caps->recovery.maximum_number_media_packets = packet[pos++];
                break;
            case AVDTP_CONTENT_PROTECTION:
                caps->content_protection.cp_type_lsb = packet[pos++];
                caps->content_protection.cp_type_msb = packet[pos++];
                caps->content_protection.cp_type_value_len = cap_len - 2;
                printf_hexdump(packet+pos, caps->content_protection.cp_type_value_len);
                pos += caps->content_protection.cp_type_value_len;
                break;
            case AVDTP_HEADER_COMPRESSION:
                caps->header_compression.back_ch  = packet[pos] >> 7; 
                caps->header_compression.media    = packet[pos] >> 6;
                caps->header_compression.recovery = packet[pos] >> 5;
                pos++;
                break;
            case AVDTP_MULTIPLEXING:
                caps->multiplexing_mode.fragmentation = packet[pos++] >> 7;
                for (i=0; i<caps->multiplexing_mode.transport_identifiers_num; i++){
                    caps->multiplexing_mode.transport_session_identifiers[i] = packet[pos++] >> 7;
                    caps->multiplexing_mode.tcid[i] = packet[pos++] >> 7;
                    // media, reporting. recovery
                }
                break;
            case AVDTP_MEDIA_CODEC:
                caps->media_codec.media_type = packet[pos++] >> 4;
                caps->media_codec.media_codec_type = packet[pos++];
                caps->media_codec.media_codec_information_len = cap_len - 2;
                printf_hexdump(packet+pos, caps->media_codec.media_codec_information_len);
                pos += caps->media_codec.media_codec_information_len;
                break;
        }
        registered_service_categories = store_bit16(registered_service_categories, category, 1);
    }
    return registered_service_categories;
}

static inline int avdtp_acceptor_send_capabilities(uint16_t cid, uint8_t transaction_label, avdtp_sep_t sep, uint8_t pack_all_capabilities){
    uint8_t command[100];
    uint8_t identifier = (uint8_t)AVDTP_SI_GET_CAPABILITIES;
    if (pack_all_capabilities){
        identifier = (uint8_t)AVDTP_SI_GET_ALL_CAPABILITIES;
    } 
    
    int pos = 0;
    command[pos++] = avdtp_header(transaction_label, AVDTP_SINGLE_PACKET, AVDTP_RESPONSE_ACCEPT_MSG);
    command[pos++] = identifier;
    int i = 0;
    for (i = 1; i < 9; i++){
        if (get_bit16(sep.registered_service_categories, i)){
            // service category
            command[pos++] = i;
            pos += avdtp_pack_service_capabilities(command+pos, sizeof(command)-pos, sep.capabilities, (avdtp_service_category_t)i, pack_all_capabilities);
        }
    }
    command[pos++] = 0x04;
    command[pos++] = 0x02;
    command[pos++] = 0x02;
    command[pos++] = 0x00;
    
    // printf(" avdtp_acceptor_send_capabilities_response: \n");
    // printf_hexdump(command, pos);
    return l2cap_send(cid, command, pos);
}

static int avdtp_acceptor_send_capabilities_response(uint16_t cid, uint8_t transaction_label, avdtp_sep_t sep){
    return avdtp_acceptor_send_capabilities(cid, transaction_label, sep, 0);
}

static int avdtp_acceptor_send_all_capabilities_response(uint16_t cid, uint8_t transaction_label, avdtp_sep_t sep){
    return avdtp_acceptor_send_capabilities(cid, transaction_label, sep, 1);
}

static int avdtp_acceptor_send_accept_response(uint16_t cid,  avdtp_signal_identifier_t identifier, uint8_t transaction_label){
    uint8_t command[2];
    command[0] = avdtp_header(transaction_label, AVDTP_SINGLE_PACKET, AVDTP_RESPONSE_ACCEPT_MSG);
    command[1] = (uint8_t)identifier;
    return l2cap_send(cid, command, sizeof(command));
}

int avdtp_acceptor_stream_config_subsm(avdtp_stream_endpoint_t * stream_endpoint, avdtp_signaling_packet_header_t * signaling_header, uint8_t *packet, uint16_t size){
    if (!stream_endpoint) return 0;
    int request_to_send = 1;

    switch (stream_endpoint->acceptor_config_state){
        case AVDTP_ACCEPTOR_STREAM_CONFIG_IDLE:
            switch (signaling_header->signal_identifier){
                case AVDTP_SI_GET_ALL_CAPABILITIES:
                    printf("    ACP: AVDTP_SI_GET_ALL_CAPABILITIES\n");
                    stream_endpoint->acceptor_config_state = AVDTP_ACCEPTOR_W2_ANSWER_GET_ALL_CAPABILITIES;
                    break;
                case AVDTP_SI_GET_CAPABILITIES:
                    printf("    ACP: AVDTP_ACCEPTOR_W2_ANSWER_GET_CAPABILITIES\n");
                    stream_endpoint->acceptor_config_state = AVDTP_ACCEPTOR_W2_ANSWER_GET_CAPABILITIES;
                    break;
                case AVDTP_SI_SET_CONFIGURATION:{
                    printf("    ACP: AVDTP_ACCEPTOR_W2_ANSWER_SET_CONFIGURATION \n");
                    avdtp_sep_t sep;
                    sep.seid = packet[3] >> 2;
                    // find or add sep
                    int i;
                    for (i=0; i < stream_endpoint->remote_seps_num; i++){
                        if (stream_endpoint->remote_seps[i].seid == sep.seid){
                            stream_endpoint->remote_sep_index = i;
                        }
                    }
                    sep.registered_service_categories = avdtp_unpack_service_capabilities(&sep.capabilities, packet+4, size-4);
                    stream_endpoint->remote_seps[stream_endpoint->remote_sep_index] = sep;
                    stream_endpoint->acceptor_config_state = AVDTP_ACCEPTOR_W2_ANSWER_SET_CONFIGURATION;
                    break;
                }
                default:
                    printf("    ACP: NOT IMPLEMENTED, Reject signal_identifier %02x\n", signaling_header->signal_identifier);
                    stream_endpoint->acceptor_config_state = AVDTP_ACCEPTOR_W2_REJECT_UNKNOWN_CMD;
                    stream_endpoint->unknown_signal_identifier = signaling_header->signal_identifier;
                    break;
            }
            break;
        case AVDTP_ACCEPTOR_STREAM_CONFIGURED:
            switch (signaling_header->signal_identifier){
                case AVDTP_SI_OPEN:
                    if (stream_endpoint->state != AVDTP_STREAM_ENDPOINT_CONFIGURED) return 0;
                    printf("    ACP: AVDTP_STREAM_ENDPOINT_W2_ANSWER_OPEN_STREAM\n");
                    stream_endpoint->acceptor_config_state = AVDTP_ACCEPTOR_W2_ANSWER_OPEN_STREAM;
                    stream_endpoint->connection->acceptor_transaction_label = signaling_header->transaction_label;
                    break;
                default:
                    printf("    ACP: AVDTP_STREAM_ENDPOINT_W2_ANSWER_OPEN_STREAM unhandled signal %d ", signaling_header->signal_identifier);
                    return 0;
            }
            break;
        case AVDTP_ACCEPTOR_W2_ANSWER_OPEN_STREAM:
            switch (signaling_header->signal_identifier){
                case AVDTP_SI_START:
                    if (stream_endpoint->state != AVDTP_STREAM_ENDPOINT_OPENED) return 0;
                    printf("    ACP: AVDTP_ACCEPTOR_W2_ANSWER_START_STREAM\n");
                    stream_endpoint->acceptor_config_state = AVDTP_ACCEPTOR_W2_ANSWER_START_STREAM;
                    stream_endpoint->connection->acceptor_transaction_label = signaling_header->transaction_label;
                    break;
                default:
                    printf("    ACP: AVDTP_ACCEPTOR_W2_ANSWER_OPEN_STREAM unhandled signal %d ", signaling_header->signal_identifier);
                    return 0;
            }
            break;

        case AVDTP_ACCEPTOR_W2_ANSWER_START_STREAM:
            printf(" AVDTP_ACCEPTOR_W2_ANSWER_START_STREAM state %d, si %d \n", stream_endpoint->state, signaling_header->signal_identifier);
            break;

        default:
            request_to_send = 0;
            break;
    }
    
    if (!request_to_send){
        printf("    ACP: NOT IMPLEMENTED\n");
    }
    return request_to_send;
}


static int avdtp_acceptor_send_response_reject(uint16_t cid,  avdtp_signal_identifier_t identifier, uint8_t transaction_label){
    uint8_t command[2];
    command[0] = avdtp_header(transaction_label, AVDTP_SINGLE_PACKET, AVDTP_GENERAL_REJECT_MSG);
    command[1] = (uint8_t)identifier;
    return l2cap_send(cid, command, sizeof(command));
}

int avdtp_acceptor_stream_config_subsm_run(avdtp_connection_t * connection, avdtp_stream_endpoint_t * stream_endpoint){
    if (!stream_endpoint) return 0;
    
    int sent = 1;
    switch (stream_endpoint->acceptor_config_state){
        case AVDTP_ACCEPTOR_STREAM_CONFIG_IDLE:
            break;
        case AVDTP_ACCEPTOR_W2_ANSWER_GET_CAPABILITIES:
            printf("    ACP: DONE\n");
            stream_endpoint->acceptor_config_state = AVDTP_ACCEPTOR_STREAM_CONFIG_IDLE;
            avdtp_acceptor_send_capabilities_response(connection->l2cap_signaling_cid, connection->acceptor_transaction_label, stream_endpoint->sep);
            break;
        case AVDTP_ACCEPTOR_W2_ANSWER_GET_ALL_CAPABILITIES:
            stream_endpoint->acceptor_config_state = AVDTP_ACCEPTOR_STREAM_CONFIG_IDLE;
            printf("    ACP: DONE\n");
            avdtp_acceptor_send_all_capabilities_response(connection->l2cap_signaling_cid, connection->acceptor_transaction_label, stream_endpoint->sep);
            break;
        case AVDTP_ACCEPTOR_W2_ANSWER_SET_CONFIGURATION:
            printf("    ACP: DONE\n");
            stream_endpoint->acceptor_config_state = AVDTP_ACCEPTOR_STREAM_CONFIGURED;
            stream_endpoint->connection = connection;
            stream_endpoint->state = AVDTP_STREAM_ENDPOINT_CONFIGURED;
            avdtp_acceptor_send_accept_response(connection->l2cap_signaling_cid, AVDTP_SI_SET_CONFIGURATION, connection->acceptor_transaction_label);
            break;
        case AVDTP_ACCEPTOR_W2_REJECT_UNKNOWN_CMD:
            stream_endpoint->acceptor_config_state = AVDTP_ACCEPTOR_STREAM_CONFIG_IDLE;
            printf("    ACP: DONE\n");
            avdtp_acceptor_send_response_reject(connection->l2cap_signaling_cid, stream_endpoint->unknown_signal_identifier, connection->acceptor_transaction_label);
            break;
       case AVDTP_ACCEPTOR_W2_ANSWER_OPEN_STREAM:
            printf("    ACP: AVDTP_STREAM_ENDPOINT_W4_L2CAP_FOR_MEDIA_CONNECTED\n");
            stream_endpoint->state = AVDTP_STREAM_ENDPOINT_W4_L2CAP_FOR_MEDIA_CONNECTED;
            avdtp_acceptor_send_accept_response(stream_endpoint->connection->l2cap_signaling_cid, AVDTP_SI_OPEN, stream_endpoint->connection->acceptor_transaction_label);
            break;
        case AVDTP_ACCEPTOR_W2_ANSWER_START_STREAM:
            printf("    ACP: AVDTP_STREAM_ENDPOINT_STREAMING \n");
            stream_endpoint->state = AVDTP_STREAM_ENDPOINT_STREAMING;
            avdtp_acceptor_send_accept_response(stream_endpoint->connection->l2cap_signaling_cid, AVDTP_SI_START, stream_endpoint->connection->acceptor_transaction_label);
            break;
     
        default:  
            printf("    ACP: NOT IMPLEMENTED\n");
            return 0;
    }
    return sent;
}
