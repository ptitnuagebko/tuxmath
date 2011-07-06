/* digraph.c
  
   Graph is represented in the form of adjaceny-lists;
   and the implementation is specific to directed graphs.
   
   Copyright 2011.
   Authors:  Siddharth Kothari
   Project email: <tuxmath-devel@lists.sourceforge.net>
   Project website: http://tux4kids.alioth.debian.org

digraph.c is part of "Tux, of Math Command", a.k.a. "tuxmath".

Tuxmath is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Tuxmath is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <stdio.h>
#include <stdlib.h>
#include "graph.h"

typedef struct node *link;

struct node {
  int v;
  link next;
};

struct graph {
  int V;
  int E;
  link *adj;
};

/* Local function prototypes */
link add(int, link);
link remov_(int, link, int *);


/* Create an edge from 1st vertice to second   */
/* @Param v - Origin vertice                   */
/* @Param w - Destination vertice              */
Edge EDGE(int v, int w) {
  Edge e;
  e.out = v;
  e.in = w;
  return e;
}

/* Initialize graph with a fixed number of     */
/* vertices                                    */
/* @Param int - The number of vertices         */
/* @Return Graph                               */
Graph graph_init(int V) {
  int v;
  Graph G = malloc(sizeof *G);
  G->V = V;
  G->E = 0;
  G->adj = malloc(V*sizeof(link));
  for (v = 0; v < V; v++)
    G->adj[v] = NULL;
  return G;
}

/* Insert a new edge in the Graph. Insertion   */
/* is O(1) since we don't check for duplicates */
/* @Param Graph, Edge                          */
void graph_insert_edge(Graph G, Edge e) {
  int vertice_out = e.out;
  int vertice_in = e.in;
  G->adj[vertice_out] = add(vertice_in, G->adj[vertice_out]);
  G->E++;
}

/* Remove an edge from the graph. Cost of      */
/* deletion is proportional to O(V) and the    */
/* implementation also removes the duplicates  */
void graph_remove_edge(Graph G, Edge e) {
  int vertice_out = e.out;
  int vertice_in = e.in;
  G->adj[vertice_out] = remov_(vertice_in, G->adj[vertice_out], &G->E);
}

/* Prints the graph on the console             */
/* @Param Graph                                */
void graph_display(Graph G) {
  int v;
  link t;
  for(v = 0; v < G->V; v++) {
    printf("%d -> ", v);
    for(t = G->adj[v]; t != NULL; t = t->next) {
      printf("%d, ", t->v);
    }
    printf("\n");
  }
}


link add(int v, link list) {
  link new = malloc(sizeof *new);
  new->v = v;
  new->next = list;
  return new;
}


link remov_(int v, link start, int *edge_counter) {

  link temp, current, previous;
  // check the start link
  if (start->v == v) {
    temp = start;
    start=start->next;
    free(temp);
    return start;
  }
  current = start->next, previous = start;

  while (current != NULL) {
    if (current->v == v) {
      temp = current;
      previous->next = current->next;
      (*edge_counter)--;
      // de-allocate memory
      free(temp);
    } 
    else previous = previous->next;
    current = current->next;
  }
  return start;
}
