CONTIKI_PROJECT = CentralUnit Node1 Node2 Node4
all : $(CONTIKI_PROJECT)
CONTIKI = /home/user/contiki
CONTIKI_WITH_RIME = 1
CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"
include $(CONTIKI)/Makefile.include