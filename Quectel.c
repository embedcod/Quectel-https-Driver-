/*
 * Quectel.c
 *
 *  Created: 11/12/2024
 *	Modified: 14/03/2025
 *    
 *		Author: ERIC MULWA
 */

#include "Quectel.h"

/**
*  @brief UART Configuration
*/
void uart_init() {
    uart_config_t uart_config = {
        .baud_rate = QUECTEL_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };

    uart_driver_install(MODEM_UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(MODEM_UART_NUM, &uart_config);
    uart_set_pin(MODEM_UART_NUM, TX_PIN, RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

/**
*  @brief Hardware Reset
*/
void gsm_reset() {
	esp_rom_gpio_pad_select_gpio(RESET_PIN);
    gpio_set_direction(RESET_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(RESET_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(RESET_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(5000));
}

/**
*  @brief Hardware Power On
*/
void gsm_poweron() {
	esp_rom_gpio_pad_select_gpio(PWR_PIN);
    gpio_set_direction(PWR_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PWR_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(500)); // minimum of 500ms
    gpio_set_level(PWR_PIN, 1); 
    vTaskDelay(pdMS_TO_TICKS(500)); 
    gpio_set_level(PWR_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(12000)); // Wait for the module to initialize (minimum of 10s)
}

/**
*  @brief Hardware Power Down
*/
void hardware_poweroff() {
	esp_rom_gpio_pad_select_gpio(PWR_PIN);
    gpio_set_direction(PWR_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PWR_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(500)); // minimum of 500ms
    gpio_set_level(PWR_PIN, 1); 
    vTaskDelay(pdMS_TO_TICKS(500));
    gpio_set_level(PWR_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(500)); 
}

/**
*  @brief AT Command Power Down
*/
void at_poweroff(){
	bool power_down = false;
    for (int attempt = 0; attempt < 3; attempt++) {
        if (!send_at_command("AT+QPOWD=1", "OK", RETRIES, CMD_DELAY_MS)) {
            ESP_LOGW(TAG_GSM, "AT command power down failed on attempt %d, retrying...", attempt + 1);
        } else {
            power_down = true;
            ESP_LOGI(TAG_GSM, "Modem powered down successfully!");
            break;
        }
        
    } if (!power_down) {
        ESP_LOGE(TAG_GSM, "AT command power down failed, exiting task...");
    }
}

/**
*  @brief Send AT Commands with response check
*/
bool send_at_command(const char *command, const char *expected_response, int retries, uint32_t timeout_ms) {

    char response[BUF_SIZE];
    for (int attempt = 0; attempt < retries; attempt++) {
        uart_write_bytes(MODEM_UART_NUM, command, strlen(command));
        uart_write_bytes(MODEM_UART_NUM, "\r\n", 2);

        uint64_t start_time = esp_timer_get_time();
        int index = 0;

        while ((esp_timer_get_time() - start_time) < (timeout_ms * 1000)) {
            uint8_t data;
            if (uart_read_bytes(MODEM_UART_NUM, &data, 1, 10 / portTICK_PERIOD_MS) > 0) {
                if (index < BUF_SIZE - 1) {
                    response[index++] = (char)data;
                }
            }
        }
        response[index] = '\0';
        ESP_LOGI(TAG_GSM, "Command: %s, Response: %s", command, response);

        if (strstr(response, expected_response)) {
            return true;
        }

        ESP_LOGW(TAG_GSM, "Attempt %d: Command '%s' failed. Retrying...", attempt + 1, command);
        vTaskDelay(pdMS_TO_TICKS(CMD_DELAY_MS));
    }

    ESP_LOGE(TAG_GSM, "Command '%s' failed after %d retries.", command, retries);
    return false;
}


/**
*  @brief GSM Communication Task
*
*  - Power On Modem
*  - Activate and set https
*  - Transmit data payload to thingsboard
*  - Close https socket
*  - Power down
*/
void gsm_comm_task() {	

    uart_init();
	char response[BUF_SIZE];

	esp_rom_gpio_pad_select_gpio(PWR_PIN);
    gpio_set_direction(PWR_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PWR_PIN, 0);
    
	esp_rom_gpio_pad_select_gpio(RESET_PIN);
    gpio_set_direction(RESET_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(RESET_PIN, 0);
    
	gsm_poweron();

	/**
	*  @brief Quectel GSM Power On Status Check
	*/
    bool power = false;
    for (int attempt = 0; attempt < 4; attempt++) {
        if (!send_at_command("AT", "OK", 3, CMD_DELAY_MS)) {
            ESP_LOGW(TAG_GSM, "Modem in Off State on check %d. Retrying...", attempt + 1);
            gsm_poweron();
            vTaskDelay(pdMS_TO_TICKS(1000)); 
        } else {
            power = true;
            ESP_LOGI(TAG_GSM, "The Modem is in the On State, Proceeding...");
            break; 
        }
    }
    if (!power) {
        ESP_LOGE(TAG_GSM, "GSM Power Status Check failed after %d retries. Shutting down and exiting...", RETRIES);
        at_poweroff();
        return;
    }
      
	/**
	*  @brief Quectel GSM Network Latch Check
	*/
    bool latch = false;
    for (int attempt = 0; attempt < 10; attempt++) {
        if (!send_at_command("AT+CGREG?", "5", RETRIES, CMD_DELAY_MS)) {
            ESP_LOGW(TAG_GSM, "No Network Latch on attempt %d. Retrying...", attempt + 1);
            vTaskDelay(pdMS_TO_TICKS(2000)); 
        } else {
            latch = true;
            ESP_LOGI(TAG_GSM, "GSM Network Latch successful!");
            break; 
        }
    }
    if (!latch) {
        ESP_LOGE(TAG_GSM, "GSM Network Latch failed after %d retries. Shutting down and exiting...", RETRIES);
        at_poweroff();
        return;
    }


	/**
	*  @brief Quectel GSM PDP Context configuration and PDP Activation
	*/
	char apncommand[60];
	sprintf(apncommand, "AT+QICSGP=1,1,\"%s\",\"\",\"\",1", APN);
    bool success = false;
    for (int attempt = 0; attempt < RETRIES; attempt++) {
        if (!send_at_command(apncommand, "OK", RETRIES, CMD_DELAY_MS) ||
            !send_at_command("AT+QIACT=1", "OK", RETRIES, 10000)) {
            ESP_LOGW(TAG_GSM, "GSM setup failed on attempt %d. Retrying...", attempt + 1);
            vTaskDelay(pdMS_TO_TICKS(2000)); 
        } else {
            success = true;
            ESP_LOGI(TAG_GSM, "GSM setup (PDP Configuration and Activation) successful!");
            break; 
        }
    }
    if (!success) {
        ESP_LOGE(TAG_GSM, "GSM setup failed after %d retries. Shutting down and exiting...", RETRIES);
        at_poweroff();
        return;
    }

	/**
	*  @brief HTTPS Configuration
	*/
    int sequence_retry_count = 0;
    while (sequence_retry_count < 3) {
        if (!send_at_command("AT+QHTTPCFG=\"contextid\",1", "OK", RETRIES, CMD_DELAY_MS) ||
            !send_at_command("AT+QSSLCFG=\"sslversion\",1,4", "OK", RETRIES, CMD_DELAY_MS) ||
            !send_at_command("AT+QSSLCFG=\"ciphersuite\",1,0xFFFF", "OK", RETRIES, CMD_DELAY_MS) ||
            !send_at_command("AT+QSSLCFG=\"seclevel\",1,0", "OK", RETRIES, CMD_DELAY_MS) ||
            !send_at_command("AT+QHTTPCFG=\"requestheader\",1", "OK", RETRIES, CMD_DELAY_MS) ||
            !send_at_command("AT+QHTTPCFG=\"responseheader\",1", "OK", RETRIES, CMD_DELAY_MS)) {
            ESP_LOGE(TAG_GSM, "https configuration failed. Exiting task.");
            return;
        } else {
            ESP_LOGI(TAG_GSM, "https configuration successful!");
            break;
        }
    }
    if (sequence_retry_count >= 3) {
        ESP_LOGE(TAG_GSM, "Max retries for https configuration sequence reached. Shutting down and exiting...");
        at_poweroff();
        return;
    }

	/**
	*  @brief Setting https url
	*/
    uint32_t chipId = getChipId();   
    const char *url = getDeviceUrl(chipId);  
	const int retries_http_url = 3;
	bool http_url_set = false;
	for (int attempt = 0; attempt < retries_http_url; attempt++) {
	    if (!send_at_command("AT+QHTTPURL=79,80", "CONNECT", RETRIES, CMD_DELAY_MS)) {
	        ESP_LOGW(TAG_GSM, "Failed to set https url on attempt %d, retrying...", attempt + 1);
	    } else {
	        uart_write_bytes(MODEM_UART_NUM, url, strlen(url));
	        vTaskDelay(pdMS_TO_TICKS(4000)); 
	        http_url_set = true;
	        ESP_LOGI(TAG_GSM, "https url set successfully on attempt %d.", attempt + 1);
	        break;
	    }	    
	} if (!http_url_set) {
	    ESP_LOGE(TAG_GSM, "Failed to set https url after %d attempts. Shutting down and exiting...", retries_http_url);
	    at_poweroff();
	    return;
	}


	/**
	*  @brief Sending https post request
	*/
	    char *jsonPayload = create_json_payload();
		//ESP_LOGI(TAG_GSM, "generated JSON payload: %s", jsonPayload); // debug
		const char *postHeader = getHttpPostHeader(chipId);
		const int retries_http_post = 3;
		bool http_post_success = false;
		char postRequest[BUF_SIZE];        	                  
		snprintf(postRequest, BUF_SIZE,
		         "%s"
		         //"Host: thingsboard.cloud\r\n"
		         "Host: things.geviton.co.ke\r\n"
		         "Content-Type: application/json\r\n"
		         "Content-Length: %d\r\n\r\n%s",
		         postHeader, (int)strlen(jsonPayload), jsonPayload);     
			int postRequestLen = strlen(postRequest);
		snprintf(response, BUF_SIZE, "AT+QHTTPPOST=%d,80,80", postRequestLen);
		for (int attempt = 0; attempt < retries_http_post; attempt++) {
		    if (!send_at_command(response, "CONNECT", RETRIES, 10000)) {
		        ESP_LOGW(TAG_GSM, "Failed to send https post request on attempt %d, retrying...", attempt + 1);
		    } else {
		        uart_write_bytes(MODEM_UART_NUM, postRequest, postRequestLen);
		        vTaskDelay(pdMS_TO_TICKS(4000));
		        http_post_success = true;
		        ESP_LOGI(TAG_GSM, "https post request sent successfully on attempt %d.", attempt + 1);
		        break;
		    }	        
		} if (!http_post_success) {
		    ESP_LOGE(TAG_GSM, "Failed to send https post request after %d attempts. Shutting down and exiting...", retries_http_post);
		    free(jsonPayload);
		    at_poweroff();
		    return;
		}

	/**
	*  @brief Deactivate PDP Context
	*/
	const int retries_pdp_deactivate = 3;
	bool pdp_deactivated = false;
	for (int attempt = 0; attempt < retries_pdp_deactivate; attempt++) {
	    if (!send_at_command("AT+QIDEACT=1", "200", RETRIES, CMD_DELAY_MS)) {
	        ESP_LOGW(TAG_GSM, "Failed to deactivate PDP context on attempt %d, retrying...", attempt + 1);
	    } else {
	        ESP_LOGI(TAG_GSM, "PDP context deactivated successfully on attempt %d.", attempt + 1);
	        pdp_deactivated = true;
	        break;
	    }    
	} if (!pdp_deactivated) {
	    ESP_LOGE(TAG_GSM, "Failed to deactivate PDP context after %d attempts. Shutting down and exiting...", retries_pdp_deactivate);
	    at_poweroff();
	    return;
	}

	/**
	*  @brief Buzzer Beep twice to indicate successful data transmission
	*  Use only if necessary
	*/
	soundBuzzertwo(80,80);
	
	/**
	*  @brief Power Down Modem
	*/
    const int retries_modem_power_down = 3;
    bool modem_powered_down = false;
    for (int attempt = 0; attempt < retries_modem_power_down; attempt++) {
        if (!send_at_command("AT+QPOWD=1", "OK", RETRIES, 3000)) {
            ESP_LOGW(TAG_GSM, "Failed to power down modem on attempt %d, retrying...", attempt + 1);
        } else {
            modem_powered_down = true;
            ESP_LOGI(TAG_GSM, "Modem powered down successfully!");
            break;
        }
        
    } if (!modem_powered_down) {
        ESP_LOGE(TAG_GSM, "AT command power down failed after %d retries. Initiating Hardware Power Down...", retries_modem_power_down);
           hardware_poweroff();
    }
    
	/**
	*  @brief Clean up
	*/
    ESP_LOGI(TAG_GSM, "GSM communication task completed successfully!");
	uart_driver_delete(MODEM_UART_NUM);
}
