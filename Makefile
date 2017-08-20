CONTIKI_PROJECT = CentralUnit Node1 Node2 
all : $(CONTIKI_PROJECT)
CONTIKI = /home/user/contiki
#PROJECT_SOURCEFILES += Node1.c Node2.c
CONTIKI_WITH_RIME = 1
#CFLAGS += -DPROJECT_CONF_H=\"project-conf.h\"
include $(CONTIKI)/Makefile.include