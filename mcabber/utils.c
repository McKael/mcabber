#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ncurses.h>
#include <stdarg.h>
#include <time.h>

/* Variables globales a UTILS.C */
int DebugEnabled = 0;

void ut_InitDebug(int level)
{
  FILE *fp = fopen("/tmp/mcabberlog", "w");

  DebugEnabled = level;
  fprintf(fp, "Debug inicializado...\n"
	  "-----------------------------------\n");
  fclose(fp);
}

void ut_WriteLog(const char *fmt, ...)
{
  FILE *fp = NULL;
  time_t ahora;
  va_list ap;
  char *buffer = NULL;

  if (DebugEnabled) {
    fp = fopen("/tmp/mcabberlog", "a+");
    buffer = (char *) calloc(1, 64);

    ahora = time(NULL);
    strftime(buffer, 64, "[%H:%M:%S] ", localtime(&ahora));
    fprintf(fp, "%s", buffer);

    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);

    free(buffer);
    fclose(fp);
  }
}

char **ut_SplitMessage(char *message, int *nsubmsgs, unsigned int maxlong)
{
  /* BUGs:    recorta la palabra si la longitud maxlong es menor que la palabra
     //  maxlong = 4
     // message = "peaso bug!"
     // submsgs[0] = "peas"
     // submsgs[1] = "bug!"
     // por lo demas, rula de arte. De todos modos, podrias verificarla ???
   */
  char *running;
  char *aux;
  char *aux2;
  char **submsgs;
  char *buffer = (char *) malloc(strlen(message) * 2);
  int i = 0;

  submsgs = (char **) malloc(50 * sizeof(char *));	/* limitamos, a priori, el maximo de lineas devueltas... */

  running = strdup(message);
  aux2 = strdup(message);

  aux = index(running, '\n');     /* is there a CR? */

  while ((strlen(aux2) > maxlong) || (aux)) {
    memset(buffer, 0, strlen(message) * 2);
    running[maxlong] = '\0';	    /* cut at $maxlong chars */

    if (aux != NULL) {              /* there is a CR */
      strncpy(buffer, running, strlen(running) - strlen(aux));
    } else {
      aux = rindex(running, ' ');   /* look for last blank */
      if (aux != NULL)		    /* hay blanco! */
        strncpy(buffer, running, strlen(running) - strlen(aux));
      else
        strcpy(buffer, running);    /* se supone que esto es pa evitar  el bug explicado arriba, pero no rula */
    }

    submsgs[i] = (char *) malloc(strlen(buffer) + 1);	/*reservamos memoria */
    strcpy(submsgs[i], buffer);	/*copiamos el buffer de arriba */
    i++;			/*aumentamos numero de mensajillos */
    aux2 += strlen(buffer) + 1;	/*eliminamos texto particionado */
    sprintf(running, "%s", aux2);	/*y lo copiamos de nuevo a la string de "curro" */
    aux = index(running, '\n');     /* is there is a CR now? */
  }
  /* last part of the message */
  if (strlen(aux2) > 0) {
    submsgs[i] = (char *) malloc(strlen(aux2) + 1);
    strcpy(submsgs[i], aux2);
    i++;
  }
  (*nsubmsgs) = i;
  free(buffer);
  return submsgs;
}

/* Desc: get the rightmost substring
 *
 * In  : string, match
 * Out : ptr to substring (or NULL if not found)
 *
 * Note: this one has no namespace, cos it belongs to <string.h>
 */
char *ut_strrstr(const char *s1, const char *s2)
{
  int l = strlen(s2);

  if (l) {
    const char *s = strchr(s1, '\0') - l;
    while (s >= s1) {
      if (*s == *s2) {
	int _l = l - 1;
	const char *_s = s + 1, *_s2 = s2 + 1;
	while (_l) {
	  if (*_s++ != *_s2++) {
	    break;
	  }
	  _l--;
	}
	if (!_l) {
	  return (char *) s;
	}
      }
      s--;
    }
  }

  return NULL;
}

char *gettag(char *buffer, char *what)
{
  char *aux;
  char *aux2;
  char *result = (char *) malloc(1024);
  char *tmp = (char *) malloc(1024);
  memset(result, 0, 1024);
  memset(tmp, 0, 1024);

  sprintf(tmp, "<%s>", what);
  aux = strstr(buffer, tmp);
  if (aux) {
    aux += strlen(tmp);
    sprintf(tmp, "</%s>", what);
    aux2 = strstr(aux, tmp);
    if (aux2) {
      strncpy(result, aux, strlen(aux) - strlen(aux2));
      free(tmp);
      return result;
    }
  }
  free(tmp);
  free(result);
  return "UNKtag";
}


char *getattr(char *buffer, char *what)
{
  char *aux;
  char *aux2;
  char *result = (char *) malloc(1024);
  memset(result, 0, 1024);

  aux = strstr(buffer, what);
  if (aux) {
    aux += strlen(what);
    aux2 = strstr(aux, "'");
    if (aux2) {
      strncpy(result, aux, strlen(aux) - strlen(aux2));
      return result;
    }
  }
  free(result);
  return "UNKattr";
}

void ut_CenterMessage(char *text, int width, char *output)
{
  char *blank;
  int ntest, nn;

  memset(output, 0, strlen(output));

  ntest = (width - strlen(text)) / 2;
  blank = (char *) malloc(ntest + 1);

  for (nn = 0; nn < ntest; nn++)
    blank[nn] = ' ';
  blank[ntest] = '\0';

  strcpy(output, blank);
  strcat(output, text);
  strcat(output, blank);

  free(blank);
}
