#include "io_mcp.h"
#include "io_config.h"
#include "i2c.h"
#include "util.h"

#include <user_interface.h>

#include <stdlib.h>

typedef struct
{
	uint32_t counter;
	uint32_t debounce;
} mcp_data_pin_t;

enum
{
	_IODIR = 0x00,
	_IPOL = 0x02,
	_GPINTEN = 0x04,
	_DEFVAL = 0x06,
	_INTCON = 0x08,
	_IOCON = 0x0a,
	_GPPU = 0x0c,
	_INTF = 0x0e,
	_INTCAP = 0x10,
	_GPIO = 0x12,
	_OLAT = 0x14,
};

#define IODIR(s)	(_IODIR + s)
#define	IPOL(s)		(_IPOL + s)
#define GPINTEN(s)	(_GPINTEN + s)
#define DEFVAL(s)	(_DEFVAL + s)
#define INTCON(s)	(_INTCON + s)
#define IOCON(s)	(_IOCON + s)
#define GPPU(s)		(_GPPU + s)
#define INTF(s)		(_INTF + s)
#define INTCAP(s)	(_INTCAP + s)
#define GPIO(s)		(_GPIO + s)
#define OLAT(s)		(_OLAT + s)

static uint8_t pin_output_cache[2];
static mcp_data_pin_t mcp_data_pin_table[io_mcp_instance_size][16];

irom static io_error_t read_register(string_t *error_message, int address, int reg, int *value)
{
	uint8_t i2cbuffer[2];
	i2c_error_t error;

	i2cbuffer[0] = reg;

	if((error = i2c_send(address, 1, &i2cbuffer[0])) != i2c_error_ok)
	{
		if(error_message)
			i2c_error_format_string(error_message, error);

		return(io_error);
	}

	if((error = i2c_receive(address, 1, &i2cbuffer[1])) != i2c_error_ok)
	{
		if(error_message)
			i2c_error_format_string(error_message, error);

		return(io_error);
	}

	*value = i2cbuffer[1];

	return(io_ok);
}

irom static io_error_t write_register(string_t *error_message, int address, int reg, int value)
{
	uint8_t i2cbuffer[2];
	i2c_error_t error;

	i2cbuffer[0] = reg;
	i2cbuffer[1] = value;

	if((error = i2c_send(address, 2, &i2cbuffer[0])) != i2c_error_ok)
	{
		if(error_message)
			i2c_error_format_string(error_message, error);

		return(io_error);
	}

	return(io_ok);
}

irom static io_error_t clear_set_register(string_t *error_message, int address, int reg, int clearmask, int setmask)
{
	uint8_t i2cbuffer[2];
	i2c_error_t error;

	i2cbuffer[0] = reg;

	if((error = i2c_send(address, 1, &i2cbuffer[0])) != i2c_error_ok)
	{
		if(error_message)
			i2c_error_format_string(error_message, error);

		return(io_error);
	}

	if((error = i2c_receive(address, 1, &i2cbuffer[1])) != i2c_error_ok)
	{
		if(error_message)
			i2c_error_format_string(error_message, error);

		return(io_error);
	}

	i2cbuffer[1] &= ~clearmask;
	i2cbuffer[1] |= setmask;

	if((error = i2c_send(address, 2, &i2cbuffer[0])) != i2c_error_ok)
	{
		if(error_message)
			i2c_error_format_string(error_message, error);

		return(io_error);
	}

	return(io_ok);
}

irom io_error_t io_mcp_init(const struct io_info_entry_T *info)
{
	int ix;
	int instance, pin;
	uint8_t i2cbuffer[0x16];
	mcp_data_pin_t *mcp_pin_data;

	if(i2c_send_1(info->address, IODIR(0)) != i2c_error_ok)
		return(io_error);

	if(i2c_receive(info->address, sizeof(i2cbuffer), i2cbuffer) != i2c_error_ok)
		return(io_error);

	for(ix = DEFVAL(0); ix < 0x0b; ix++)
		if(i2cbuffer[ix] != 0x00)
			return(io_error);

	for(ix = 0x0e; ix < 0x0f; ix++)
		if(i2cbuffer[ix] != 0x00)
			return(io_error);

	for(instance = 0; instance < io_mcp_instance_size; instance++)
	{
		for(pin = 0; pin < 16; pin++)
		{
			mcp_pin_data = &mcp_data_pin_table[instance][pin];

			mcp_pin_data->counter = 0;
			mcp_pin_data->debounce = 0;
		}
	}

	pin_output_cache[0] = 0;
	pin_output_cache[1] = 0;

	return(io_ok);
}

irom void io_mcp_periodic(int io, const struct io_info_entry_T *info, io_data_entry_t *data, io_flags_t *flags)
{
	int pin;
	int intf[2], intcap[2];
	int bank, bankpin;
	mcp_data_pin_t *mcp_pin_data;
	io_config_pin_entry_t *pin_config;

	read_register((string_t *)0, info->address, INTF(0), &intf[0]);
	read_register((string_t *)0, info->address, INTF(1), &intf[1]);

	read_register((string_t *)0, info->address, INTCAP(0), &intcap[0]);
	read_register((string_t *)0, info->address, INTCAP(1), &intcap[1]);

	for(pin = 0; pin < 16; pin++)
	{
		bank = (pin & 0x08) >> 3;
		bankpin = pin & 0x07;

		mcp_pin_data = &mcp_data_pin_table[info->instance][pin];
		pin_config = &config.io_config[io][pin];

		if(pin_config->llmode == io_pin_ll_counter)
		{
			if(mcp_pin_data->debounce != 0)
			{
				if(mcp_pin_data->debounce >= 10)
					mcp_pin_data->debounce -= 10; // 10 ms per tick
				else
					mcp_pin_data->debounce = 0;
			}
			else
			{
				if((intf[bank] & (1 << bankpin)) && !(intcap[bank] & (1 << bankpin))) // only count downward edge, counter is mostly pull-up
				{
					mcp_pin_data->counter++;
					mcp_pin_data->debounce = pin_config->speed;
					flags->counter_triggered = 1;
				}
			}
		}
	}
}

irom io_error_t io_mcp_init_pin_mode(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	int bank, bankpin;

	bank = (pin & 0x08) >> 3;
	bankpin = pin & 0x07;

	if(clear_set_register(error_message, info->address, IPOL(bank), 1 << bankpin, 0) != io_ok) // polarity inversion = 0
		return(io_error);

	if(clear_set_register(error_message, info->address, GPINTEN(bank), 1 << bankpin, 0) != io_ok) // pc int enable = 0
		return(io_error);

	if(clear_set_register(error_message, info->address, DEFVAL(bank), 1 << bankpin, 0) != io_ok) // compare value = 0
		return(io_error);

	if(clear_set_register(error_message, info->address, INTCON(bank), 1 << bankpin, 0) != io_ok) // compare source = 0
		return(io_error);

	if(clear_set_register(error_message, info->address, GPPU(bank), 1 << bankpin, 0) != io_ok) // pullup = 0
		return(io_error);

	if(clear_set_register(error_message, info->address, GPIO(bank), 1 << bankpin, 0) != io_ok) // gpio = 0
		return(io_error);

	if(clear_set_register(error_message, info->address, OLAT(bank), 1 << bankpin, 0) != io_ok) // latch = 0
		return(io_error);

	switch(pin_config->llmode)
	{
		case(io_pin_ll_disabled):
		{
			break;
		}

		case(io_pin_ll_input_digital):
		case(io_pin_ll_counter):
		{
			if(clear_set_register(error_message, info->address, IODIR(bank), 0, 1 << bankpin) != io_ok) // direction = 1
				return(io_error);

			if(pin_config->flags.pullup && (clear_set_register(error_message, info->address, GPPU(bank), 0, 1 << bankpin) != io_ok))
				return(io_error);

			if((pin_config->llmode == io_pin_ll_counter) && (clear_set_register(error_message, info->address, GPINTEN(bank), 0, 1 << bankpin) != io_ok)) // pc int enable = 1
				return(io_error);

			break;
		}

		case(io_pin_ll_output_digital):
		{
			if(clear_set_register(error_message, info->address, IODIR(bank), 1 << bankpin, 0) != io_ok) // direction = 0
				return(io_error);

			break;
		}

		default:
		{
			if(error_message)
				string_cat(error_message, "invalid mode for this pin\n");

			return(io_error);
		}
	}

	return(io_ok);
}

irom io_error_t io_mcp_get_pin_info(string_t *dst, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin)
{
	int bank, bankpin, tv;
	int io, olat, cached;
	mcp_data_pin_t *mcp_pin_data;

	bank = (pin & 0x08) >> 3;
	bankpin = pin & 0x07;

	mcp_pin_data = &mcp_data_pin_table[info->instance][pin];

	switch(pin_config->llmode)
	{
		case(io_pin_ll_input_analog):
		{
			if(read_register(dst, info->address, GPIO(bank), &tv) != io_ok)
				return(io_error);

			string_format(dst, "current io: %s", onoff(tv & (1 << bankpin)));

			break;
		}

		case(io_pin_ll_counter):
		{
			if(read_register(dst, info->address, GPIO(bank), &tv) != io_ok)
				return(io_error);

			string_format(dst, "current io: %s, debounce: %d", onoff(tv & (1 << bankpin)), mcp_pin_data->debounce);

			break;
		}

		case(io_pin_ll_output_digital):
		{
			if(read_register(dst, info->address, GPIO(bank), &tv) != io_ok)
				return(io_error);

			io = tv & (1 << bankpin);

			if(read_register(dst, info->address, OLAT(bank), &tv) != io_ok)
				return(io_error);

			olat = tv & (1 << bankpin);
			cached = pin_output_cache[bank] & (1 << bankpin);

			string_format(dst, "current latch: %s, io: %s, cache: %s", onoff(io), onoff(olat), onoff(cached));

			break;
		}

		default:
		{
		}
	}

	return(io_ok);
}

irom io_error_t io_mcp_read_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, int *value)
{
	int bank, bankpin, tv;
	mcp_data_pin_t *mcp_pin_data;

	bank = (pin & 0x08) >> 3;
	bankpin = pin & 0x07;

	mcp_pin_data = &mcp_data_pin_table[info->instance][pin];

	switch(pin_config->llmode)
	{
		case(io_pin_ll_input_digital):
		case(io_pin_ll_output_digital):
		{
			if(read_register(error_message, info->address, GPIO(bank), &tv) != io_ok)
				return(io_error);

			*value = !!(tv & (1 << bankpin));

			break;
		}

		case(io_pin_ll_counter):
		{
			*value = mcp_pin_data->counter;

			break;
		}

		default:
		{
			if(error_message)
				string_cat(error_message, "invalid mode for this pin\n");

			return(io_error);
		}
	}

	return(io_ok);
}

irom io_error_t io_mcp_write_pin(string_t *error_message, const struct io_info_entry_T *info, io_data_pin_entry_t *pin_data, const io_config_pin_entry_t *pin_config, int pin, int value)
{
	int bank, bankpin;
	mcp_data_pin_t *mcp_pin_data;

	bank = (pin & 0x08) >> 3;
	bankpin = pin & 0x07;

	mcp_pin_data = &mcp_data_pin_table[info->instance][pin];

	if(value)
		pin_output_cache[bank] |= 1 << bankpin;
	else
		pin_output_cache[bank] &= ~(1 << bankpin);

	switch(pin_config->llmode)
	{
		case(io_pin_ll_output_digital):
		{
			if(write_register(error_message, info->address, GPIO(bank), pin_output_cache[bank]) != io_ok)
				return(io_error);

			break;
		}

		case(io_pin_ll_counter):
		{
			mcp_pin_data->counter = value;

			break;
		}

		default:
		{
			if(error_message)
				string_cat(error_message, "invalid mode for this pin\n");

			return(io_error);
		}
	}

	return(io_ok);
}
