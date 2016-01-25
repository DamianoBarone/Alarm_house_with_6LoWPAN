/*
 * A generic node expose the battery resource and can simulate the IR sensor by
 * the button.
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
#define DEBUG DEBUG_NONE
#include "net/ip/uip-debug.h"
#include "dev/button-sensor.h"

#define UDP_PORT		1234
#define DECREASE_RATE	20	// decrease battery level from 0 to DECREASE_RATE
// interval at which we resend the alert to the dead node
#define SEND_INTERVAL	(CLOCK_SECOND)
char alarm='0'; // the alarm status: 1 if alarm tripped, 0 otherwise
static unsigned int batt=100; // battery level, start from full charged battery
// timer used to send periodic message to the guardian node
static struct ctimer periodic_timer;
// connection used to send message to the guardina node
static struct simple_udp_connection unicast_connection;

/*---------------------------------------------------------------------------*/
#if (DEBUG) & DEBUG_PRINT
void print_addr(const uip_ipaddr_t *a) {
	printf("%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x", a->u8[0], a->u8[1], a->u8[2], a->u8[3], a->u8[4], a->u8[5], a->u8[6], a->u8[7], a->u8[8], a->u8[9], a->u8[10], a->u8[11], a->u8[12], a->u8[13], a->u8[14], a->u8[15]);
}
#endif
/*---------------------------------------------------------------------------*/
/*
 * When we receive a message from the guardian node as response
 * to our message for notify the alarm, stop the timer and unset the alarm flag.
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
#if (DEBUG) & DEBUG_PRINT
  printf("funzione receiver\nData received on port %d from port %d with length %d\n", receiver_port, sender_port, datalen);
#endif

  /* ack received, data should contain "ok"
	   because in our network the only UDP message a generic node can receiv
	   is the ack from the guardian node we skip the check on the sender IP
	   and on the payload:
	   
	   if (sender_addr==guardian_node_IP && data=="ok")
	*/

  alarm='0'; // unset alarm flag
  ctimer_stop(&periodic_timer); // stop the timer
}
/*---------------------------------------------------------------------------*/
/*
 * If the alarm flag is set send a UDP message to the guardina node,
 * then set a timer to call itself after SEND_INTERVAL seconds.
 *
 * @parameter par, the IP of the dead node
 */
void send_alert(void *par)
{
  uip_ipaddr_t guardian_node_addr;
  char buf[sizeof(uip_ipaddr_t)+1];
  uip_ipaddr_t *dead_addr=(uip_ipaddr_t *) par;

#if (DEBUG) & DEBUG_PRINT  
  printf("funxione send alert, alarm=%c\n", alarm);
  printf("ip: ");
  if (par)
  	print_addr(dead_addr);
  else
  	printf("NULL");
  printf("\n");
#endif
  
  if (alarm == '0') // if alarm flag is not set, return
	return ;

  // guardian node address
  uip_ip6addr(&guardian_node_addr, 0xaaaa, 0, 0, 0, 0xc30c, 0, 0, 0x0002);
  
  if (!par) // payload is zeros if IR sensor tripped
    memset(buf, 0, sizeof(uip_ipaddr_t)+1);
  else // if alarm tripped payload is the IP of the unreachable node
  {
    memcpy(buf, dead_addr, sizeof(uip_ipaddr_t));
    buf[sizeof(uip_ipaddr_t)]=0;
  }

  // send the UDP message to the guardian node
  simple_udp_sendto(&unicast_connection, buf, sizeof(uip_ipaddr_t) + 1, &guardian_node_addr);
  
  ctimer_stop(&periodic_timer);
  // set timer for the next execution
  ctimer_set(&periodic_timer, SEND_INTERVAL, &send_alert, (void *)dead_addr);
}
/*---------------------------------------------------------------------------*/
/*
 * when this function is called set the alarm flag and start the loop of
 * reports to the guardian node through the send_alert() function.
 *
 * @parameter par, the IP of the dead node
 */
void
dead_node(void* par)
{
#if (DEBUG) & DEBUG_PRINT
  printf("funxione dead node\n");
#endif
  alarm='1'; // set alarm flag
  send_alert(par); // start to send alert message to the guardian node
}
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

PROCESS(rest_server_example, "Erbium Example Server");
AUTOSTART_PROCESSES(&rest_server_example);

PROCESS_THREAD(rest_server_example, ev, data)
{
  PROCESS_BEGIN();

  simple_udp_register(&unicast_connection, UDP_PORT, NULL, UDP_PORT, receiver);
  
  SENSORS_ACTIVATE(button_sensor);
  
  PRINTF("Starting Erbium Example Server\n");
  PRINTF("uIP buffer: %u\n", UIP_BUFSIZE);
  PRINTF("LL header: %u\n", UIP_LLH_LEN);
  PRINTF("IP+UDP header: %u\n", UIP_IPUDPH_LEN);
  PRINTF("REST max chunk: %u\n", REST_MAX_CHUNK_SIZE);

  // initialize the REST engine
  rest_init_engine();

  // activate the resource
  rest_activate_resource(&batttery, "batttery");
  
  // set the function to be called when a node becomes unreachable
  set_handler_function(dead_node);
#if (DEBUG) & DEBUG_PRINT
  printf("RISORSA PRONTA.\n");
#endif
  while(1) {
    PROCESS_WAIT_EVENT();
	if (data == &button_sensor) // IR sensor tripped
		dead_node((void *)NULL);
  } /* while (1) */

  PROCESS_END();
}
