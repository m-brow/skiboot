/******************************************************************************
 * Copyright (c) 2004, 2008 IBM Corporation
 * All rights reserved.
 * This program and the accompanying materials
 * are made available under the terms of the BSD License
 * which accompanies this distribution, and is available at
 * http://www.opensource.org/licenses/bsd-license.php
 *
 * Contributors:
 *     IBM Corporation - initial implementation
 *****************************************************************************/

#include <stdbool.h>
#include <compiler.h>
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "ctype.h"



struct custom_format {
	/* Custom specifier string */
	char *format_specifier;
	/* Print function takes the struct and returns a char pointer */
	int (*print_func) (char **buffer, size_t bufsize, void *value);
};


static void *custom_print_start = &__print_start;

static const unsigned long long convert[] = {
	0x0, 0xFF, 0xFFFF, 0xFFFFFF, 0xFFFFFFFF,
	0xFFFFFFFFFFULL, 0xFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL
};

static int
print_str_fill(char **buffer, size_t bufsize, char *sizec,
					const char *str, char c)
{
	size_t i, sizei, len;
	char *bstart = *buffer;

	sizei = strtoul(sizec, NULL, 10);
	len = strlen(str);
	if (sizei > len) {
		for (i = 0;
			(i < (sizei - len)) && ((*buffer - bstart) < bufsize);
									i++) {
			**buffer = c;
			*buffer += 1;
		}
	}
	return 1;
}

static int
print_str(char **buffer, size_t bufsize, const char *str)
{
	char *bstart = *buffer;
	size_t i;

	for (i = 0; (i < strlen(str)) && ((*buffer - bstart) < bufsize); i++) {
		**buffer = str[i];
		*buffer += 1;
	}
	return 1;
}

static unsigned int __attrconst
print_intlen(unsigned long value, unsigned short int base)
{
	int i = 0;

	while (value > 0) {
		value /= base;
		i++;
	}
	if (i == 0)
		i = 1;
	return i;
}

static int
print_itoa(char **buffer, size_t bufsize, unsigned long value,
					unsigned short base, bool upper)
{
	const char zeichen[] = {'0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
	char c;
	int i, len;

	if(base <= 2 || base > 16)
		return 0;

	len = i = print_intlen(value, base);

	/* Don't print to buffer if bufsize is not enough. */
	if (len > bufsize)
		return 0;

	do {
		c = zeichen[value % base];
		if (upper)
			c = toupper(c);

		(*buffer)[--i] = c;
		value /= base;
	} while(value);

	*buffer += len;

	return 1;
}



static int
print_fill(char **buffer, size_t bufsize, char *sizec, unsigned long size,
				unsigned short int base, char c, int optlen)
{
	int i, sizei, len;
	char *bstart = *buffer;

	sizei = strtoul(sizec, NULL, 10);
 	len = print_intlen(size, base) + optlen;
	if (sizei > len) {
		for (i = 0;
			(i < (sizei - len)) && ((*buffer - bstart) < bufsize);
									i++) {
			**buffer = c;
			*buffer += 1;
		}
	}

	return 0;
}

/*
 * Check if input string matches or is a substring of the
 * custom specifiers.
 *
 * Return true is strings match / are substrings.
 * If strings match exactly, the res pointer is set to the corresponding
 * custom format specifier struct.
 */
static bool check_specifier_substring(const char *format,
				      struct custom_format **res)
{
	int list_len, format_len, spec_len, i;
	struct custom_format *print_fmt;
	char *spec;

	print_fmt = (struct custom_format *)custom_print_start;
	list_len  = (&__print_start - &__print_end) / 
			sizeof(struct custom_format);
	format_len = strlen(format);

	for (i = 0; i < list_len; i++) {
		spec = print_fmt[i].format_specifier;
		spec_len = strlen(spec);

		/* Input string is larger than specifier string */
		if (format_len > spec_len)
			continue;

		/* if spec_len == format_len */
		if (!strcmp(spec, format)) {
			*res = &print_fmt[i];
			return true;
		}
		/* if spec_len > format_len */
		if (!strncmp(spec, format, format_len)) {
			return true;
		}
	
	}
	return false;
}

#if 0
/*
 * Check if custom print specifier is valid.
 * Returns a pointer to the corresponding custom_format struct
 * if they match.
 * Return NULL pointer if no custom formats match.
 */
static struct custom_format *check_custom_specifiers(const char *format)
{
	int i, j, list_len, format_len, spec_len;
	char *spec;
	struct custom_format *custom_print = 
		(struct custom_format *)custom_print_start;

	len = (&__print_start - &__print_end) / sizeof(struct custom_format);
	format_len = strlen(format);

	for (i = 0; i < len; i++) {
		spec = (char *)custom_print[i].format_specifier;
		spec_len = strlen(spec);

		// if format_len < spec_len
		j = 0;

		while (spec[j] != '\0' && format[j] != '\0') {
			if (spec[j] != format[j])
				continue;
			j++;
		}
		return (custom_print + i);
	}

	return NULL;
}
#endif

static int
print_format(char **buffer, size_t bufsize, const char *format, void *var)
{
	struct custom_format *cust_format;
	char *start;
	unsigned int i = 0, length_mod = sizeof(int);
	unsigned long value = 0;
	unsigned long signBit;
	char *form, sizec[32];
	char sign = ' ';
	bool upper = false;
	bool valid = false;
	form  = (char *) format;
	start = *buffer;

	form++;
	if(*form == '0' || *form == '.') {
		sign = '0';
		form++;
	}

	valid = check_specifier_substring(format, &cust_format);

	/* Print the custom format specifier */
	if (valid && cust_format) {
		cust_format->print_func	(buffer, bufsize, var);
		return (long int) (*buffer - start);
	}

	while ((*form != '\0') && ((*buffer - start) < bufsize)) {
		switch(*form) {
			case 'u':
			case 'd':
			case 'i':
				sizec[i] = '\0';
				value = (unsigned long) var;
				signBit = 0x1ULL << (length_mod * 8 - 1);
				if ((*form != 'u') && (signBit & value)) {
					**buffer = '-';
					*buffer += 1;
					value = (-(unsigned long)value) & convert[length_mod];
				}
				print_fill(buffer, bufsize - (*buffer - start),
						sizec, value, 10, sign, 0);
				print_itoa(buffer, bufsize - (*buffer - start),
							value, 10, upper);
				break;
			case 'X':
				upper = true;
				/* fallthrough */
			case 'x':
				sizec[i] = '\0';
				value = (unsigned long) var & convert[length_mod];
				print_fill(buffer, bufsize - (*buffer - start),
						sizec, value, 16, sign, 0);
				print_itoa(buffer, bufsize - (*buffer - start),
							value, 16, upper);
				break;
			case 'O':
			case 'o':
				sizec[i] = '\0';
				value = (long int) var & convert[length_mod];
				print_fill(buffer, bufsize - (*buffer - start),
						sizec, value, 8, sign, 0);
				print_itoa(buffer, bufsize - (*buffer - start),
							value, 8, upper);
				break;
			case 'p':
				sizec[i] = '\0';
				print_fill(buffer, bufsize - (*buffer - start),
					sizec, (unsigned long) var, 16, ' ', 2);
				print_str(buffer, bufsize - (*buffer - start),
									"0x");
				print_itoa(buffer, bufsize - (*buffer - start),
						(unsigned long) var, 16, upper);
				break;
			case 'c':
				sizec[i] = '\0';
				print_fill(buffer, bufsize - (*buffer - start),
							sizec, 1, 10, ' ', 0);
				**buffer = (unsigned long) var;
				*buffer += 1;
				break;
			case 's':
				sizec[i] = '\0';
				print_str_fill(buffer,
					bufsize - (*buffer - start), sizec,
							(char *) var, ' ');

				print_str(buffer, bufsize - (*buffer - start),
								(char *) var);
				break;
			case 'l':
				form++;
				if(*form == 'l') {
					length_mod = sizeof(long long int);
				} else {
					form--;
					length_mod = sizeof(long int);
				}
				break;
			case 'h':
				form++;
				if(*form == 'h') {
					length_mod = sizeof(signed char);
				} else {
					form--;
					length_mod = sizeof(short int);
				}
				break;
			case 'z':
				length_mod = sizeof(size_t);
				break;
			default:
				if(*form >= '0' && *form <= '9')
					sizec[i++] = *form;
		}
		form++;
	}

	
	return (long int) (*buffer - start);
}
#if 0
/*
 * Check if the format specifier is at the end of a sequence.
 * Return false if we reach a terminating character of the specifier.
 */
static bool check_format_specifier(char *format, char *buff, int length)
{
	struct custom_format *fmt_check;
	char *fullstr;

	char last = format[length - 1];

	if (last == '\0' || last == ' ')
		return false;

	/* Check if format matches are custom specifiers */
	// should return true if it is a substring
	bool term_char = check_specifier_substring(format, &fmt_check);
	
	/* Matches a custom specifier */
	if (term_char && fmt_check)
		return false;

	/* Substring of custom specifier */
	if (term_char)
		return true;

	/* Need case for when there is an invalid custom specifier e.g. %px */
	
#if 1
	if (last == 'p') {
		// concat the format and buff strings
		// buff - length = format[0];

		char small_str = buff[0];
		fullstr = strcat(format, &small_str);
		term_char = check_specifier_substring(fullstr, &fmt_check);

		/* String is not a substring of a custom specifier
		 * Stop looking for 
		 */
//		if (!term_char)
//			return false;
	}
#endif
	/* Check for terminating format specifier characters */
	if (last == 'd' || last == 'i' || last == 'u' || last == 'x' ||
	    last == 'X' || last == 'c' || last == 's' || last == '%' ||
	    last == 'O' || last == 'o')
		return false;

	return true;
}
#endif
/*
 * Using ptr look for any matching custom specifiers.
 * If we find a matching one, write it to 'format'.
 * Return true - custom specifier found
 */
static bool find_custom_specifiers(char **format, char **ptr, int *i)
{
	struct custom_format *fmt;
	char tmp_fmt[15];
	char *fmt_str;
	bool is_cust;
	char *spec;

	strncpy(tmp_fmt, *ptr, 15);
	
	is_cust = check_specifier_substring(tmp_fmt, &fmt);

	if (!is_cust || (is_cust && !fmt))
		return false;

	/* Using the fmt struct, copy the fmt.format_specifier to format
	 * Also increment the ptr and i values by the length of the specifier
	 */

	spec = fmt->format_specifier;
	fmt_str = *format;

	while (*spec != '\0') {
		*fmt_str = *spec;
		fmt_str++;
		spec++;
		(*ptr)++;
		(*i)++;
	}
	*fmt_str = '\0';


	return true;
}

/*
 * The vsnprintf function prints a formatted strings into a buffer.
 * BUG: buffer size checking does not fully work yet
 */
int
vsnprintf(char *buffer, size_t bufsize, const char *format, va_list arg)
{
	char *ptr, *bstart;

	bstart = buffer;
	ptr = (char *) format;

	/*
	 * Return from here if size passed is zero, otherwise we would
	 * overrun buffer while setting NULL character at the end.
	 */
	if (!buffer || !bufsize)
		return 0;

	/* Leave one space for NULL character */
	bufsize--;

	while(*ptr != '\0' && (buffer - bstart) < bufsize)
	{
		if(*ptr == '%') {
			char formstr[20];
			int i = 0;
			/* Finds any custom specifiers */
			bool cust = find_custom_specifiers((char **)&formstr, &ptr, &i);

			if (!cust) {
				do {
					formstr[i] = *ptr;
					ptr++;
					i++;
				} while(!(*ptr == 'd' || *ptr == 'i' ||
					  *ptr == 'u' || *ptr == 'x' ||
					  *ptr == 'X' || *ptr == 'p' ||
					  *ptr == 's' || *ptr == '%' ||
					  *ptr == 'O' || *ptr == 'o'));

				/* Add last char to buffer*/
				formstr[i++] = *ptr;
				formstr[i] = '\0';
			}

			if(*ptr == '%') {
				*buffer++ = '%';
			} else {
				/*
				 * This changes the format specifier into the
				 * actual string.
				 */
				print_format(&buffer,
					bufsize - (buffer - bstart),
					formstr, va_arg(arg, void *));
			}
			ptr++;
		} else {

			*buffer = *ptr;

			buffer++;
			ptr++;
		}
	}
	
	*buffer = '\0';

	return (buffer - bstart);
}
