#include <stdio.h>
#include <stdlib.h> 
//https://github.com/Emiliano0506/API-informatica-1-1R1.git
#include <string.h> //malloc,free,realloc
#include <unistd.h> //memcopy,strlen
#include <ctype.h>  //caracteres
#include <stdint.h>  //enteros exacto 64
#include <inttypes.h> //para imprimir los int64
#include <curl/curl.h> //acceder a curl
#include <cjson/cJSON.h> //acceder a json

#define LOGFILE "bot.log"

struct memory {
    char *response;
    size_t size;
}; //ingresa texto y cuantos bytes tiene

/* Callback para guardar respuesta HTTP */
static size_t write_cb(char *data, size_t size, size_t nmemb, void *clientp) {
    size_t realsize = size * nmemb; //culcula cuantos bytes llego
    struct memory *mem = clientp;

    char *ptr = realloc(mem->response, mem->size + realsize + 1);
    if (!ptr) 
       return 0;

    mem->response = ptr; //agranda
    memcpy(&(mem->response[mem->size]), data, realsize);

    mem->size += realsize;
    mem->response[mem->size] = 0;
    return realsize; //copia
}

/* Leer token desde archivo */
int read_token(const char *filename, char *token_out) {
    FILE *f = fopen(filename, "r");
    if (!f) 
       return 0;
    fgets(token_out, 200, f);
    token_out[strcspn(token_out, "\n")] = 0; //saca \n
    fclose(f);
    return 1;
}

/* Guardar registro */
void log_message(long date, const char *name, const char *msg) {
    FILE *f = fopen(LOGFILE, "a");
    if (!f) return;
    fprintf(f, "%ld | %s | %s\n", date, name, msg);
    fclose(f);
}

/* strcasestr para Windows */
char *strcasestr_win(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
            h++; n++;
        }
        if (!*n) return (char *)haystack;
    }
    return NULL; //se usa para detectar hola o chau
}

/* Enviar mensaje usando POST con escape */
void send_message(const char *token, int64_t chat_id, const char *msg) {
    CURL *curl = curl_easy_init(); //crea un curl
    if (!curl) return;

    char url[512];
    snprintf(url, sizeof(url),
             "https://api.telegram.org/bot%s/sendMessage", token); //le llega el url

    char *escaped = curl_easy_escape(curl, msg, 0); //escapa
    if (!escaped) {
        curl_easy_cleanup(curl);
        return;
    }

    char postfields[1024];
    snprintf(postfields, sizeof(postfields),
             "chat_id=%" PRId64 "&text=%s", chat_id, escaped);//le llega el chat id y text
    curl_free(escaped);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postfields);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        fprintf(stderr, "Error al enviar mensaje: %s\n", curl_easy_strerror(res));
    }

    curl_easy_cleanup(curl);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Debe pasar el archivo con el token.\n");
        return 1;
    }

    char token[200];
    if (!read_token(argv[1], token)) {
        printf("No se pudo leer el token.\n");
        return 1;
    } //leer token

    curl_global_init(CURL_GLOBAL_ALL); //inicia curl
    long offset = 0;

    while (1) {
        //inicia bucle infinito
        CURL *curl = curl_easy_init();
        if (!curl) continue;

        char url[1024];
        snprintf(url, sizeof(url),
            "https://api.telegram.org/bot%s/getUpdates?timeout=1&offset=%ld",
            token, offset);
        //recibe mensajes nuevos
        struct memory chunk = {0};
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (chunk.size == 0) {
            sleep(2);
            continue;
        }

        cJSON *json = cJSON_Parse(chunk.response); //interpetrar el json del tele
        if (!json) {
            free(chunk.response);
            sleep(2);
            continue;
        }

        cJSON *result = cJSON_GetObjectItem(json, "result");
        int array_size = cJSON_GetArraySize(result);

        for (int i = 0; i < array_size; i++) {
            cJSON *update = cJSON_GetArrayItem(result, i);
            cJSON *update_id_json = cJSON_GetObjectItem(update, "update_id");
            if (!update_id_json) continue;
            long update_id = update_id_json->valuedouble;
            if (update_id >= offset) offset = update_id + 1;

            cJSON *message = cJSON_GetObjectItem(update, "message");
            if (!message) continue;

            cJSON *text_json = cJSON_GetObjectItem(message, "text");
            if (!text_json) continue;
            char *text = text_json->valuestring;

            cJSON *chat = cJSON_GetObjectItem(message, "chat");
            if (!chat) continue;

            cJSON *chat_id_json = cJSON_GetObjectItem(chat, "id");
            if (!chat_id_json) continue;
            int64_t chat_id = (int64_t)chat_id_json->valuedouble;

            cJSON *first_name_json = cJSON_GetObjectItem(chat, "first_name");
            char *first_name = first_name_json ? first_name_json->valuestring : "Usuario";

            cJSON *date_json = cJSON_GetObjectItem(message, "date");
            long date = date_json ? date_json->valuedouble : 0;

            log_message(date, first_name, text);

            /* Debug: mostrar chat_id y mensaje */
            printf("DEBUG: chat_id=%" PRId64 ", mensaje='%s'\n", chat_id, text);

            if (strcasestr_win(text, "hola")) {
                char msg_resp[256];
                snprintf(msg_resp, sizeof(msg_resp), "Hola, %s!", first_name);
                send_message(token, chat_id, msg_resp);
            } else if (strcasestr_win(text, "chau")) {
                send_message(token, chat_id, "¡Hasta luego!");
            }
        }

        cJSON_Delete(json);
        free(chunk.response);
        sleep(2);
    }

    curl_global_cleanup();
    return 0;
}


