#define STR_MALLOC_AND_COPY(OUTSTR, INSTR) do {(OUTSTR) = gasneti_malloc(strlen(INSTR)+1); strcpy((OUTSTR), (INSTR));} while(0)

myxml_node_t *myxml_createNode_attr_list(myxml_node_t* parent, char *tag, char **attribute_list, char **attribute_vals, int num_attributes, char *value) {
  int i,j;
  myxml_node_t *ret=gasneti_calloc(1,sizeof(myxml_node_t));
  ret->parent = parent;
  ret->num_children = 0;
  /*make sure that we aren't adding to a leaf or know that this is the root node*/
  if(parent==NULL) {
    ret->nodeclass = MYXML_ROOT_NODE;
  } else if(parent->nodeclass == MYXML_LEAF_NODE) {
    fprintf(stderr, "can't add a child to a leaf node!\n");
    exit(1);
  }

  if(tag==NULL) {
    fprintf(stderr, "tag can't be null!\n");
    exit(1);
  } else {
    STR_MALLOC_AND_COPY(ret->tag, tag); 
  } 
  
  /*this mustbe a leaf node since an explicit value was declared*/
  if(value) {
    STR_MALLOC_AND_COPY(ret->value, value); 
    ret->nodeclass = MYXML_LEAF_NODE;
  } else if(parent!=NULL)  {
    ret->nodeclass = MYXML_INTER_NODE;
  }
  
  ret->attribute_list = gasneti_malloc(sizeof(myxml_attribute_t)*num_attributes);
  
  for(i=0; i<num_attributes; i++) {
    STR_MALLOC_AND_COPY(ret->attribute_list[i].attribute_name, attribute_list[i]);
    STR_MALLOC_AND_COPY(ret->attribute_list[i].attribute_value, attribute_vals[i]);
  }
  
  /*add myself to my parents children list*/
  if(parent) {
    parent->num_children++;
    parent->children = gasneti_realloc(parent->children,parent->num_children*sizeof(myxml_node_t*));
    parent->children[parent->num_children-1] = ret;
  }
  
  return ret;
}


void myxml_addAttribute(myxml_node_t *node, char *attribute_name, char *attribute_value) {

  node->attribute_list = gasneti_realloc(node->attribute_list, sizeof(myxml_attribute_t)*(node->num_attributes+1));
  STR_MALLOC_AND_COPY(node->attribute_list[node->num_attributes].attribute_name, attribute_name);
  STR_MALLOC_AND_COPY(node->attribute_list[node->num_attributes].attribute_value, attribute_value);
  node->num_attributes++;
}

void myxml_addAttributeInt(myxml_node_t *node, char *attribute_name, int attribute_value) {
  char buffer[50];
  sprintf(buffer, "%d", attribute_value);
  node->attribute_list = gasneti_realloc(node->attribute_list, sizeof(myxml_attribute_t)*(node->num_attributes+1));
  STR_MALLOC_AND_COPY(node->attribute_list[node->num_attributes].attribute_name, attribute_name);
  STR_MALLOC_AND_COPY(node->attribute_list[node->num_attributes].attribute_value, buffer);
  node->num_attributes++;
}

myxml_node_t *myxml_createNode(myxml_node_t* parent, char *tag, char *attribute, char *attribute_val, char *value) {
  myxml_node_t *ret = myxml_createNode_attr_list(parent, tag, NULL, NULL, 0, value);
  if(attribute!=NULL && attribute_val!=NULL) myxml_addAttribute(ret, attribute, attribute_val);
  return ret;
}

myxml_node_t *myxml_createNodeInt(myxml_node_t* parent, char *tag, char *attribute, int attribute_val, char *value) {
  char buffer[100];
  myxml_node_t *ret = myxml_createNode_attr_list(parent, tag, NULL, NULL, 0, value);
  sprintf(buffer, "%d", attribute_val);
  myxml_addAttribute(ret, attribute, buffer);
  return ret;
}

void myxml_destroyTree(myxml_node_t *node) {
  int i,j;
  
  for(i=0; i<node->num_children; i++) {
    myxml_destroyTree(node->children[i]);
  }
  gasneti_free(node->children);
  for(j=0; j<node->num_attributes; j++) {
    gasneti_free(node->attribute_list[j].attribute_name);
    gasneti_free(node->attribute_list[j].attribute_value);
  }
  gasneti_free(node->attribute_list);
  if(node->tag) gasneti_free(node->tag);
  if(node->value) gasneti_free(node->value);
  gasneti_free(node);
  return;
}

void myxml_printTreeXML_helper(FILE *outstream, myxml_node_t *node, int level, char *whitespace) {
  int i, l;
  
  for(l=0; l<level; l++) {
    fprintf(outstream, "%s", whitespace);
  } 
  fprintf(outstream, "<%s", node->tag);
  for(i=0; i<node->num_attributes; i++) {
    fprintf(outstream, " %s=\"%s\"", node->attribute_list[i].attribute_name, node->attribute_list[i].attribute_value);
  }
  fprintf(outstream, ">\n");
  if(node->nodeclass == MYXML_LEAF_NODE) {
    for(l=0; l<level+1; l++) {
      fprintf(outstream, "%s", whitespace);
    }
    fprintf(outstream, "%s\n", node->value);
  } else {
    for(i=0; i<node->num_children; i++) {
      myxml_printTreeXML_helper(outstream, node->children[i], level+1, whitespace); 
    }
  }
  
  for(l=0; l<level; l++) {
    fprintf(outstream, "%s", whitespace);
  }
  fprintf(outstream, "</%s>\n", node->tag);
  
}


void myxml_printTreeXML(FILE *outstream, myxml_node_t *node, char *whitespace) {
  fprintf(outstream, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");

  myxml_printTreeXML_helper(outstream, node, 0, whitespace);
}

