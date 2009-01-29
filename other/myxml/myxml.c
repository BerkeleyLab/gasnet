

#define STR_ALLOC_AND_COPY(OUTSTR, INSTR) do {(OUTSTR) = gasneti_malloc(strlen(INSTR)+1); strcpy((OUTSTR), (INSTR));} while(0)

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
    STR_ALLOC_AND_COPY(ret->tag, tag); 
  } 
  
  /*this mustbe a leaf node since an explicit value was declared*/
  if(value) {
    STR_ALLOC_AND_COPY(ret->value, value); 
    ret->nodeclass = MYXML_LEAF_NODE;
  } else if(parent!=NULL)  {
    ret->nodeclass = MYXML_INTER_NODE;
  }
  
  ret->attribute_list = gasneti_malloc(sizeof(myxml_attribute_t)*num_attributes);
  
  for(i=0; i<num_attributes; i++) {
    STR_ALLOC_AND_COPY(ret->attribute_list[i].attribute_name, attribute_list[i]);
    STR_ALLOC_AND_COPY(ret->attribute_list[i].attribute_value, attribute_vals[i]);
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
  /*if adding an attribute name and value can't be NULL*/
  if(attribute_name == NULL || attribute_value == NULL) {
    fprintf(stderr, "myxml error: attribute_name and attribute value must be non null when adding new attribute!\n");
    exit(1);
  }
  STR_ALLOC_AND_COPY(node->attribute_list[node->num_attributes].attribute_name, attribute_name);
  STR_ALLOC_AND_COPY(node->attribute_list[node->num_attributes].attribute_value, attribute_value);
  node->num_attributes++;
}

void myxml_addAttributeInt(myxml_node_t *node, char *attribute_name, int attribute_value) {
  char buffer[50];
   if(attribute_name == NULL ) {
    fprintf(stderr, "myxml error: attribute_name must be non null when adding new attribute!\n");
    exit(1);
  }
  sprintf(buffer, "%d", attribute_value);
  node->attribute_list = gasneti_realloc(node->attribute_list, sizeof(myxml_attribute_t)*(node->num_attributes+1));
  STR_ALLOC_AND_COPY(node->attribute_list[node->num_attributes].attribute_name, attribute_name);
  STR_ALLOC_AND_COPY(node->attribute_list[node->num_attributes].attribute_value, buffer);
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
  if(node==NULL) return;
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
  fprintf(outstream, "<%s id=%d", node->tag, node->id);
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

/*dump the tree in a human readable XML format*/
void myxml_printTreeXML(FILE *outstream, myxml_node_t *node, char *whitespace) {
  fprintf(outstream, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
  if(node != NULL) 
    myxml_printTreeXML_helper(outstream, node, 0, whitespace);
}


#define SAFE_READ(PTR, NBYTES, INSTREAM) do {                           \
    size_t __ret;                                                       \
    __ret = fread(PTR, 1, NBYTES, INSTREAM);                            \
    if(__ret != (NBYTES)) {                                             \
      fprintf(stderr, "read error (expected: %d got: %d)", (uint32_t)NBYTES, (uint32_t)__ret); \
      fclose(INSTREAM);                                                 \
      exit(1);                                                          \
    }} while(0)

   

myxml_node_t* myxml_loadTreeHelper(FILE *instream, myxml_node_t *parent_node) {
  uint32_t temp;
  int i=0;
  myxml_node_t *curr_node = (myxml_node_t*) gasneti_malloc(sizeof(myxml_node_t));
  
  curr_node->parent = parent_node;
  if(parent_node == NULL) {
    curr_node->nodeclass = MYXML_ROOT_NODE;
  } else {
    curr_node->nodeclass = MYXML_INTER_NODE;
  } 
  
  SAFE_READ(&temp, sizeof(uint32_t), instream);
  curr_node->id = ntohl(temp);
    
  SAFE_READ(&temp, sizeof(uint32_t), instream);
  curr_node->num_children = ntohl(temp);
  
  SAFE_READ(&temp, sizeof(uint32_t), instream);
  curr_node->num_attributes = ntohl(temp);

  /*read the tag length and allocate the buffer*/
  SAFE_READ(&temp, sizeof(uint32_t), instream);
  temp = ntohl(temp);
  curr_node->tag = (char*) gasneti_malloc(temp);
  /*read the tag*/
  SAFE_READ(curr_node->tag, temp, instream);
  
  curr_node->attribute_list = (myxml_attribute_t*) gasneti_malloc(sizeof(myxml_attribute_t)*curr_node->num_attributes);
  for(i=0; i<curr_node->num_attributes; i++) {
    /*read the length of the string*/
    SAFE_READ(&temp, sizeof(uint32_t), instream);
    temp = ntohl(temp);
    curr_node->attribute_list[i].attribute_name = gasneti_malloc(temp);
    SAFE_READ(curr_node->attribute_list[i].attribute_name, temp, instream);
    
    
    SAFE_READ(&temp, sizeof(uint32_t), instream);
    temp = ntohl(temp);
    curr_node->attribute_list[i].attribute_value = gasneti_malloc(temp);
    SAFE_READ(curr_node->attribute_list[i].attribute_value, temp, instream);
  }
  /*read the size of the value str*/
  SAFE_READ(&temp, sizeof(uint32_t), instream);
  temp = ntohl(temp);
  
  if(temp > 0) {
    curr_node->value = (char*) gasneti_malloc(temp);
    SAFE_READ(curr_node->value, temp, instream);
    curr_node->nodeclass = MYXML_LEAF_NODE;
  }
  
  
  
  curr_node->children = (myxml_node_t**) gasneti_malloc(sizeof(myxml_node_t*)*curr_node->num_children);
  for(i=0; i<curr_node->num_children; i++) {
    
    curr_node->children[i] = myxml_loadTreeHelper(instream, curr_node);
  }
  
  return curr_node;
  
}



myxml_node_t* myxml_loadTreeBIN(FILE *instream) {
   uint32_t *offset_idx;
   uint32_t num_nodes;
   uint32_t temp;
   int i;
   SAFE_READ(&temp, sizeof(uint32_t), instream);
   num_nodes = ntohl(temp);
   
   
   return myxml_loadTreeHelper(instream, NULL);
}



uint32_t myxml_countAndLabelNodes(myxml_node_t *node, uint32_t label) {
  int i;
  uint32_t count = 0;
  uint32_t num_children=0;
  node->id = label;
  /*add one for myself*/
  label++;
  
  for(i=0; i<node->num_children; i++) {
    num_children = myxml_countAndLabelNodes(node->children[i], label);
    label+=num_children;
    count += num_children;
  } 
  /*add one for myself*/
  return count+1;
}


#define SAFE_WRITE(PTR, NBYTES, OUTSTREAM) do { \
  size_t __ret;                                    \
  __ret = fwrite(PTR, 1, NBYTES, OUTSTREAM);  \
 if(__ret != (NBYTES)) {                              \
   fprintf(stderr, "write error (expected: %d got: %d)\n", (uint32_t)NBYTES, (uint32_t)__ret); \
   fclose(OUTSTREAM);                                             \
   exit(1); }} while(0)


#if 0
void myxml_fillSizes(myxml_node_t *node, size_t *sz_arr) {
  int i,j;
  size_t mysz;
  /*first figure out the size of mynode*/
  
  
  mysz = sizeof(uint32_t)*3; /*id, num_children, num_attributes*/
  mysz += sizeof(uint32_t)*node->num_children;
  mysz += sizeof(uint32_t) + strlen(node->tag)+1; /*tag is always non null*/
  
  for(i=0; i<node->num_attributes; i++) {
    mysz += sizeof(uint32_t) + strlen(node->attribute_list[i].attribute_name)+1;
    mysz +=sizeof(uint32_t) +  strlen(node->attribute_list[i].attribute_value)+1;
  }
  mysz+=sizeof(uint32_t) + (node->value!=NULL ? strlen(node->value)+1 : 0); 
  sz_arr[node->id] = mysz;
  
  /*and then recurse through my children*/
  for(i=0; i<node->num_children; i++) {
    myxml_fillSizes(node->children[i], sz_arr);
  }
  return;
  
}
#endif

void dump_TreeBIN(FILE *outstream, myxml_node_t *node) {
  uint32_t temp;
  int i;

  temp = htonl(node->id);
  SAFE_WRITE(&temp, sizeof(uint32_t), outstream);




  temp = htonl(node->num_children);
  SAFE_WRITE(&temp, sizeof(uint32_t), outstream);
  
  temp = htonl(node->num_attributes);
  SAFE_WRITE(&temp, sizeof(uint32_t), outstream);

  temp = htonl(strlen(node->tag)+1);
  SAFE_WRITE(&temp, sizeof(uint32_t), outstream);
  SAFE_WRITE(node->tag, strlen(node->tag)+1, outstream);
  for(i=0; i<node->num_attributes; i++) {
    temp = htonl(strlen(node->attribute_list[i].attribute_name)+1);
    SAFE_WRITE(&temp, sizeof(uint32_t), outstream);
    SAFE_WRITE(node->attribute_list[i].attribute_name, strlen(node->attribute_list[i].attribute_name)+1, outstream);
    temp = htonl(strlen(node->attribute_list[i].attribute_value)+1);
    SAFE_WRITE(&temp, sizeof(uint32_t), outstream);
    SAFE_WRITE(node->attribute_list[i].attribute_value, strlen(node->attribute_list[i].attribute_value)+1, outstream);
  }

  if(node->value) {
    temp = htonl(strlen(node->value)+1);
    SAFE_WRITE(&temp, sizeof(uint32_t), outstream);
    SAFE_WRITE(node->value, strlen(node->value)+1, outstream);
  } else {
    temp = htonl(0);
    SAFE_WRITE(&temp, sizeof(uint32_t), outstream);
  }
  
  for(i=0; i<node->num_children; i++) {
    dump_TreeBIN(outstream, node->children[i]);
  }
}


/*dump the tree as a Binary file that the other parts of GASNet can later load*/
void myxml_printTreeBIN(FILE *outstream, myxml_node_t *node) {
  /*first go through and count the number of nodes and the get the size of each one*/
  int num_nodes = myxml_countAndLabelNodes(node, 0);
  int i;
  uint32_t temp;
  

  temp = htonl(num_nodes);
  SAFE_WRITE(&temp, sizeof(uint32_t), outstream);

  dump_TreeBIN(outstream, node);

  fprintf(stdout, "tree size: %d nodes\n", num_nodes);
  
}

#undef SAFE_READ
#undef SAFE_WRITE
