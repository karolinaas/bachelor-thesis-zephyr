// TODO Copyright header goes here

/*
 * INCLUDES
 */

#include <stdint.h>

/*
 * MACRO DEFINITIONS
 */

 #define BUNDLE "#bundle"

/*
 * STRUCTS
 */

typedef struct osc_message {
	uint32_t *addressPattern;
	uint32_t *typeTag;
	uint32_t *arguments;
} osc_message;

 /*
  * Functions
  */