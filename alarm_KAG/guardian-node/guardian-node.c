/*
 * The guardian node expose the alarm resource and can enable/disable the alarm
 * when it receive the commando from the server.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contiki.h"
#include "contiki-net.h"
#include "simple-udp.h"
#include "rest-engine.h"
#include "er-coap.h"

#include "net/rpl/rpl.h"
#define DEBUG DEBUG_PRINT
#include "net/ip/uip-debug.h"
#include "dev/button-sensor.h"


#define UDP_PORT		1234
#define DECREASE_RATE	5 // decrease battery level from 0 to DECREASE_RATE
static int alarm_alert=0; // 1 if the alarm tripped, 0 otherwise
static int alarm_status=0; // 1 if the alarm is actived, 0 otherwise
static int type_of_alert=0; /* 1 if a node is unreachable, 2 if IR sensor
							   tripped, 0 otherwise */
static unsigned int batt=100; // battery level, start from full charged battery
uip_ipaddr_t *dead_node_addr=0; /* contain the IP of the node interested by the
								   alarm_status, not significant if
								   alarm_status is 0 */
// connection used to communicate with the other node in the 6LoWPan
static struct simple_udp_connection unicast_connection;

/*---------------------------------------------------------------------------*/
#if (DEBUG) & DEBUG_PRINT
void print_addr(const uip_ipaddr_t *a) {
	printf("%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x", a->u8[0], a->u8[1], a->u8[2], a->u8[3], a->u8[4], a->u8[5], a->u8[6], a->u8[7], a->u8[8], a->u8[9], a->u8[10], a->u8[11], a->u8[12], a->u8[13], a->u8[14], a->u8[15]);
}
#endif
/*---------------------------------------------------------------------------*/
/*
 * Decreased the battery level, each time the value is requested,
 * by a random value between 1 and DECREASE_RATE.
 * the message consist of a string conteining the value in % of the battery.
 */
void
batttery_handler(void* request, void* response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
  int length;
  unsigned int diff=((unsigned int)rand())%DECREASE_RATE+1;
  if (batt)
  	batt-= diff<=batt ? diff : diff%batt+1;
  length=snprintf((char *)buffer, preferred_size, "%u", batt);

  REST.set_header_content_type(response, REST.type.TEXT_PLAIN);
  REST.set_header_etag(response, (uint8_t *) &length, 1);
  REST.set_response_payload(response, buffer, length);
}
/*---------------------------------------------------------------------------*/
/*
 * Battery resource, rappresent the state of the battery in %.
 */
RESOURCE(batttery,
         "title=\"Batttery level: ?len=0..\";rt=\"Text\"",
         batttery_handler,
         NULL,
         NULL,
         NULL);
/*---------------------------------------------------------------------------*/
static void alarm_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
static void alarm_event_handler();
static void alarm_put_handler(void* request, void* response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset);
/*---------------------------------------------------------------------------*/
/*
 * Alarm resource, get handler and event handler is used to trasmitt the alarm
 * status to the server, put handler is used to turn on/off the integrity.
 */
EVENT_RESOURCE(alarm,
               "title=\"Alarm\";obs",
               alarm_get_handler,
               NULL,
               alarm_put_handler,
               NULL,
               alarm_event_handler);
/*---------------------------------------------------------------------------*/
/*
 * When a node notify the alarm status to the guardian node, we save the type
 * of notification and the IP concerned.
 */
static void
receiver(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  char buf[3];
  char zero[sizeof(uip_ipaddr_t)+1];

  strcpy(buf, "ok"); // response to the node that notify the alarm
  buf[2]=0;

#if (DEBUG) & DEBUG_PRINT
  printf("funzione receiver\n");
  printf("Data received on port %d from port %d with length %d\n", receiver_port, sender_port, datalen);
  printf("s_ip: ");
  print_addr(sender_addr);
  printf("\nr_ip: ");
  print_addr(receiver_addr);
  printf("\n");
#endif
  
  // send the response
  simple_udp_sendto(&unicast_connection, buf, 3, sender_addr);
  
  memset(zero, 0, sizeof(uip_ipaddr_t)+1);
  if (!memcmp(zero, data, sizeof(uip_ipaddr_t)+1)) { // IR sensor tripped
	dead_node_addr=(uip_ipaddr_t *)receiver_addr; /* save the IP of the sender,
 after the simple_udp_sendto the sender IP is moved to the receiver parameter */
	type_of_alert=2; // IR sensor tripped
  }
  else {
    dead_node_addr=(uip_ipaddr_t *)data;
    type_of_alert=1; // integrity fail
  }

#if (DEBUG) & DEBUG_PRINT
  printf("IP MORTO: ");
  print_addr(dead_node_addr);
  printf("\n");
#endif

  alarm_alert=1; // set the alarm flag
  alarm.trigger(); // call alarm_event_handler() to notify the subscribers
}
/*---------------------------------------------------------------------------*/
/*
 * Function called when integrity fail.
 * 
 * @parameter par, IP of the unreachable node.
 */
void
dead_node(void* par)
{
#if (DEBUG) & DEBUG_PRINT
  printf("E' MORTO: ");
  print_addr(par);
  printf("\n");
#endif
  dead_node_addr=(uip_ipaddr_t *)par;
  alarm_alert=1; // set alarm flag
  alarm.trigger(); // call alarm_event_handler()
}
/*---------------------------------------------------------------------------*/
/*
 * The resource is a string and consist of <type_of_alert dead_node_IP>,
 * type_of_alert can be 1 if a node is unreachable or 2 if IR sensor tripped.
 * In the first case dead_node_IP is the IP of the unreachable node, in the
 * second case dead_node_IP is the IP if the node which IR sensor is tripped.
 */
static void
alarm_get_handler(void *request, void *response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset)
{
#if (DEBUG) & DEBUG_PRINT
  printf("alarm_get_handler %d \n",alarm_alert);
#endif
  int lenx;
  if (alarm_alert) { // if alarm is tripped 
  	REST.set_header_content_type(response, REST.type.TEXT_PLAIN);
  	if (type_of_alert==1) // integrity failed
  		lenx=snprintf((char *)buffer, preferred_size, "1 %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x", dead_node_addr->u8[0], dead_node_addr->u8[1], dead_node_addr->u8[2], dead_node_addr->u8[3], dead_node_addr->u8[4], dead_node_addr->u8[5], dead_node_addr->u8[6], dead_node_addr->u8[7], dead_node_addr->u8[8], dead_node_addr->u8[9], dead_node_addr->u8[10], dead_node_addr->u8[11], dead_node_addr->u8[12], dead_node_addr->u8[13], dead_node_addr->u8[14], dead_node_addr->u8[15]);
  	else // type_of_alert == 2, IR sensor tripped
  		lenx=snprintf((char *)buffer, preferred_size, "2 %02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x", dead_node_addr->u8[0], dead_node_addr->u8[1], dead_node_addr->u8[2], dead_node_addr->u8[3], dead_node_addr->u8[4], dead_node_addr->u8[5], dead_node_addr->u8[6], dead_node_addr->u8[7], dead_node_addr->u8[8], dead_node_addr->u8[9], dead_node_addr->u8[10], dead_node_addr->u8[11], dead_node_addr->u8[12], dead_node_addr->u8[13], dead_node_addr->u8[14], dead_node_addr->u8[15]);
  	
  	REST.set_response_payload(response, buffer, lenx);
    alarm_alert=0; // servlet received the alert, so we can unset the flag
  }
}
/*---------------------------------------------------------------------------*/
/*
 * Notify the registered observers which will perform a CoAP get message.
 */
static void
alarm_event_handler()
{
#if (DEBUG) & DEBUG_PRINT
  printf("alarm_event_handler %d \n",alarm_alert);
#endif
  if(alarm_alert) // if alarm is tripped notify to the subscribers
    REST.notify_subscribers(&alarm);
}
/*---------------------------------------------------------------------------*/
/*
 * When the servlet send the command to enable/disable the integrity the
 * put handler call start_kag() or stop_kag().
 */
static void
alarm_put_handler(void* request, void* response, uint8_t *buffer, uint16_t preferred_size, int32_t *offset) {
  const uint8_t *data; // data received from the servlet
  
#if (DEBUG) & DEBUG_PRINT
  printf("PUT HANDLER!");
#endif
  
  REST.get_request_payload(request, &data);
#if (DEBUG) & DEBUG_PRINT
  printf("DATA RECEIVED: %c\n", *data);
#endif
  if (*data=='1' && alarm_status==0) {
#if (DEBUG) & DEBUG_PRINT
  	printf("START KAG FROM PUT HANDLER!");
#endif
	start_kag();
	alarm_status=1; // now alarm is active
  }
  if (*data=='0' && alarm_status!=0) {
#if (DEBUG) & DEBUG_PRINT
	printf("STOP KAG FROM PUT HANDLER!");
#endif
	stop_kag();
	alarm_status=0; // alarm stopped
  }
#if (DEBUG) & DEBUG_PRINT
  printf("end put handler\n");
#endif
}
/*---------------------------------------------------------------------------*/
PROCESS(rest_server_example, "Erbium Example Server");
AUTOSTART_PROCESSES(&rest_server_example);

PROCESS_THREAD(rest_server_example, ev, data)
{
  uip_ipaddr_t my_addr;
  uip_ip6addr(&my_addr, 0xaaaa, 0, 0, 0, 0xc30c, 0, 0, 0x0002);
  
  PROCESS_BEGIN();

  simple_udp_register(&unicast_connection, UDP_PORT, NULL, UDP_PORT, receiver);
  
  PRINTF("Starting Erbium Example Server\n");
  SENSORS_ACTIVATE(button_sensor);

  PRINTF("uIP buffer: %u\n", UIP_BUFSIZE);
  PRINTF("LL header: %u\n", UIP_LLH_LEN);
  PRINTF("IP+UDP header: %u\n", UIP_IPUDPH_LEN);
  PRINTF("REST max chunk: %u\n", REST_MAX_CHUNK_SIZE);

  // initialize the REST engine
  rest_init_engine();
  rest_activate_resource(&alarm, "alarm"); // alarm resource
  rest_activate_resource(&batttery, "batttery"); // battery resource

  // set the function to be called when a node becomes unreachable
  set_handler_function(dead_node);
#if (DEBUG) & DEBUG_PRINT
  printf("KAG handler set.\n");
#endif
  while(1) {
    PROCESS_YIELD();
    if (ev == sensors_event && data == &button_sensor) { // IR sensor tripped
      type_of_alert=2; // set alarm flag
      dead_node((void *)&my_addr); // notify the alarm
    }
  }

  PROCESS_END();
}

