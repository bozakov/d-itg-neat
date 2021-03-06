#include "neat_server.h"
#include "../common/util.h"
#include "../common/ITG.h"
#include "../common/timestamp.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <cstdlib> 

extern "C" {
    #include "../../neat/neat.h"
    #include "../../neat/neat_internal.h"
}

/*******************************
Discard server
*******************************/
static uint32_t config_buffer_size = 128;
static uint16_t config_log_level = 0;
static char *config_property = "{\
    \"transport\": [\
        {\
            \"value\": \"SCTP\",\
            \"precedence\": 1\
        },\
        {\
            \"value\": \"TCP\",\
            \"precedence\": 1\
        }\
    ]\
}";

unsigned char *buffer = NULL;
uint32_t buffer_filled = 0;

struct info *infos = (struct info *) calloc(logbuffer_size, sizeof(info));

struct timeval RcvTime;

int is_logging;

struct neat_ctx *ctx = NULL;

struct neat_flow *flow = NULL;

struct neat_flow_operations ops;

char* n_out;

int pkt_c = 0;

int do_tstart = 0;

extern int optind;

inline void flushBuffer(ofstream * pointerLog, struct info *infos, int &count)
{
	if ((*pointerLog).is_open()){ 
		(*pointerLog).write((const char *) infos, count * sizeof(struct info));
		(*pointerLog).flush();	
		count = 0;
	}
}

//Print usage and exit
static void
print_usage()
{
    if (config_log_level >= 2) {
        fprintf(stderr, "%s()\n", __func__);
    }

    printf("server_discard [OPTIONS]\n");
    printf("\t- P <filename> \tneat properties, default properties:\n%s\n", config_property);
    printf("\t- S \tbuffer in byte (%d)\n", config_buffer_size);
    printf("\t- v \tlog level 0..2 (%d)\n", config_log_level);
}


//Error handler
static neat_error_code
on_error(struct neat_flow_operations *opCB)
{
    if (config_log_level >= 2) {
        fprintf(stderr, "%s()\n", __func__);
    }

    exit(EXIT_FAILURE);
}


/*
    Read data until buffered_amount == 0 - then stop event loop!
*/
static neat_error_code
on_readable(struct neat_flow_operations *opCB)
{

    if(is_logging > 0 && do_tstart){
        TSTART(_tstart, secs, msecs, first, 0, RECEIVER);
        do_tstart = 1;
    }

    // data is available to read
    neat_error_code code;

    if (config_log_level >= 2) {
        (stderr, "%s()\n", __func__);
    }

    code = neat_read(opCB->ctx, opCB->flow, buffer, config_buffer_size, &buffer_filled, NULL, 0);
    if (code != NEAT_OK) {
        if (code == NEAT_ERROR_WOULD_BLOCK) {
            if (config_log_level >= 1) {
                printf("on_readable - NEAT_ERROR_WOULD_BLOCK\n");
            }
            return NEAT_OK;
        } else {
            fprintf(stderr, "%s - neat_read error: %d\n", __func__, (int)code);
            return on_error(opCB);
        }
    }

    if (buffer_filled > 0) {
        if(is_logging > 0) {
            unsigned char *ptrSeqNum = buffer + sizeof(uint32_t);			
            unsigned char *ptrTimeSec = ptrSeqNum + sizeof(uint32_t);		
            unsigned char *ptrTimeUsec = ptrTimeSec + sizeof(uint32_t);	

            GET_TIME_OF_DAY(&RcvTime, _tend, _tstart, secs, msecs, 0, RECEIVER);

            int net_TimeSec =   ntohl(*(uint32_t *) ptrTimeSec); 
            int net_TimeUsec =  ntohl(*(uint32_t *) ptrTimeUsec); 

            writeInBufferStandard(&infos[pkt_c], *(uint32_t *) buffer,
								*(uint32_t *) ptrSeqNum,
								"<unknown>", "<unknown>", get_flow_protocol(opCB->flow),
                                ntohs(0), ntohs(8080) , net_TimeSec,
								RcvTime.tv_sec % 86400L, net_TimeUsec, RcvTime.tv_usec, buffer_filled);	
            pkt_c++;

            if(pkt_c == logbuffer_size){
                flushBuffer((ofstream *) n_out, infos, pkt_c); 
            }

        }
        if (config_log_level >= 1) {
            printf("received data - %d byte\n", buffer_filled);
        }
    } else {          
        if (config_log_level >= 1) {
            printf("peer disconnected\n");
        }
        opCB->on_readable = NULL;
        neat_set_operations(opCB->ctx, opCB->flow, opCB);
        neat_close(opCB->ctx, opCB->flow);
    }
    return NEAT_OK;
}

static neat_error_code
on_connected(struct neat_flow_operations *opCB)
{
    if (config_log_level >= 2) {
        fprintf(stderr, "%s()\n", __func__);
    }

    if (config_log_level >= 1) {
        printf("peer connected\n");
    }

    opCB->on_readable = on_readable;
    neat_set_operations(opCB->ctx, opCB->flow, opCB);
    return NEAT_OK;
}

void print_logs()
{
    if (config_log_level >= 2) {
        fprintf(stderr, "%s()\n", __func__);
    }

    if(infos != NULL && is_logging > 0){
        flushBuffer((ofstream *) n_out, infos, pkt_c); 
    }
}

void neat_stop_event_loop()
{
    if (config_log_level >= 2) {
        fprintf(stderr, "%s()\n", __func__);
    }

    neat_stop_event_loop(ctx);
}

void* neat_server_start(void* param) 
{
    if (config_log_level >= 2) {
        fprintf(stderr, "%s()\n", __func__);
    }

    int arg, result;
    char *arg_property = NULL;
    neatParserParams *para = (neatParserParams *)param;
    is_logging = para->log;


    memset(&ops, 0, sizeof(ops));

    result = EXIT_SUCCESS;

    // Reset optind so we can make getopt work multiple times during the same execution.
    optind = 1;

    while ((arg = getopt(para->optc, para->neatOpt, "P:S:v:")) != -1) {
        switch(arg) {
        case 'P':
            if (read_file(optarg, &arg_property) < 0) {
                fprintf(stderr, "Unable to read properties from %s: %s",
                        optarg, strerror(errno));
                result = EXIT_FAILURE;
                goto cleanup;
            }
            if (config_log_level >= 1) {
                printf("option - properties: %s\n", arg_property);
            }
            break;
        case 'S':
            config_buffer_size = atoi(optarg);
            if (config_log_level >= 1) {
                printf("option - buffer size: %d\n", config_buffer_size);
            }
            break;
        case 'v':
            config_log_level = atoi(optarg);
            if (config_log_level >= 1) {
                printf("option - log level: %d\n", config_log_level);
            }
            break;
        default:
            print_usage();
            goto cleanup;
            break;
        }
    }

    if ((buffer = (unsigned char*) malloc(config_buffer_size)) == NULL) {
        fprintf(stderr, "%s - malloc failed\n", __func__);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    if ((ctx = neat_init_ctx()) == NULL) {
        fprintf(stderr, "%s - neat_init_ctx failed\n", __func__);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    // new neat flow
    if ((flow = neat_new_flow(ctx)) == NULL) {
        fprintf(stderr, "%s - neat_new_flow failed\n", __func__);
        result = EXIT_FAILURE;
        goto cleanup;
    }
     
    // set properties
    if (neat_set_property(ctx, flow, arg_property ? arg_property : config_property)) {
        fprintf(stderr, "%s - neat_set_property failed\n", __func__);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    // set callbacks
    ops.on_connected = on_connected;
    ops.on_error = on_error;

    if (neat_set_operations(ctx, flow, &ops)) {
        fprintf(stderr, "%s - neat_set_operations failed\n", __func__);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    // wait for on_connected or on_error to be invoked
    if (neat_accept(ctx, flow, 8080, NULL, 0)) {
        fprintf(stderr, "%s - neat_accept failed\n", __func__);
        result = EXIT_FAILURE;
        goto cleanup;
    }

    neat_start_event_loop(ctx, NEAT_RUN_DEFAULT);

    // cleanup
cleanup:

    if (arg_property)
        free(arg_property);
    
    if(buffer == NULL)
        free(buffer);
    if (ctx != NULL) {
        neat_free_ctx(ctx);
    }

    pthread_exit(0);
    return NULL;

}
