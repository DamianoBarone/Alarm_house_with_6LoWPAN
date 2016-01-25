all: generic-node
# use this target explicitly if requried: er-plugtest-server

CONTIKI=../../../..

# linker optimizations
SMALL=1

APPS += er-coap
APPS += rest-engine

# for some platforms
UIP_CONF_IPV6=1
# IPv6 make config disappeared completely
CFLAGS += -DUIP_CONF_IPV6=1

CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"

CFLAGS += -DUIP_CONF_TCP=0

CFLAGS += -DKEEP_ALIVE_GUARD

include $(CONTIKI)/Makefile.include
