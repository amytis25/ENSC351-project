#ifndef DISCORDALERT_H
#define DISCORDALERT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <curl/curl.h>


bool discordStart(const char *webhook_url);
void discordCleanup(void);
void sendDiscordAlert(const char *webhookURL, const char *msg);

#endif
