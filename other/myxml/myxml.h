#ifndef __MYXML_H__
#define __MYXML_H__ 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct myxml_node_t_;
typedef struct myxml_node_t_ myxml_node_t;

myxml_node_t *myxml_createNode_attr_list(myxml_node_t* parent, const char *tag, const char **attribute_list, const char **attribute_vals, int num_attributes, const char *value);
myxml_node_t *myxml_createNode(myxml_node_t* parent, const char *tag, const char *attribute, const char *attribute_val, const char *value);
myxml_node_t *myxml_createNodeInt(myxml_node_t* parent, const char *tag, const char *attribute, int attribute_val, const char *value);

void myxml_addAttribute(myxml_node_t *node, const char *attribute_name, const char *attribute_value);
void myxml_addAttributeInt(myxml_node_t *node, const char *attribute_name, int attribute_value);

void myxml_printTreeXML(FILE *outstream, myxml_node_t *node, const char *whitespace);
void myxml_printTreeBIN(FILE *outstream, myxml_node_t *node);

myxml_node_t* myxml_loadTreeBIN(FILE *instream);



#endif
