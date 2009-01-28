#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  MYXML_ROOT_NODE = 0,
  MYXML_LEAF_NODE,
  MYXML_INTER_NODE,
  MYXML_NUM_NODE_CLASSES} myxml_node_class_t;

typedef struct myxml_attribute_t_ {
  char *attribute_name;
  char *attribute_value;
} myxml_attribute_t;

typedef struct myxml_node_t_{
  struct myxml_node_t_ *parent;
  struct myxml_node_t_ **children;
  int num_children;
  myxml_node_class_t nodeclass;
  
  char *tag;

  myxml_attribute_t *attribute_list;
  int num_attributes;
  
  char* value;

} myxml_node_t;

myxml_node_t *myxml_createNode_attr_list(myxml_node_t* parent, char *tag, char **attribute_list, char **attribute_vals, int num_attributes, char *value);
myxml_node_t *myxml_createNode(myxml_node_t* parent, char *tag, char *attribute, char *attribute_val, char *value);
myxml_node_t *myxml_createNodeInt(myxml_node_t* parent, char *tag, char *attribute, int attribute_val, char *value);

void myxml_addAttribute(myxml_node_t *node, char *attribute_name, char *attribute_value);
void myxml_addAttributeInt(myxml_node_t *node, char *attribute_name, int attribute_value);

void myxml_printTreeXML(FILE *outstream, myxml_node_t *node, char *whitespace);
void myxml_printTreeBIN(FILE *outstream, myxml_node_t *node);



