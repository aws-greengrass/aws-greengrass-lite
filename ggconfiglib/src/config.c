#include "ggconfig.h"
#include <sqlite3.h>
#include <stdbool.h>

static bool configInitialized = false;

/* note keypath is a null terminated string. */
static int countKeyPathDepth(const char *keypath)
{
    int count = 0;
    for(char *c = keypath; *c != 0; c++)
    {
        if(*c == '/'){
            count ++;
        }
    }
    return count;
}

void makeConfigurationReady(void)
{
    if(configInitialized = false)
    {
        /* do configuration */
    }
}

bool isKnownComponent(const char *component)
{
    /* check the component list and see if this one is present */
}

bool validateKeys(const char * component, const char *key)
{
    /* check the keypath and verify that the key path exists for the component */
}

GglError ggconfig_writeValueToKey(const char *key, const char *value, const char *component)
{
    makeConfigurationReady();
    if( validateKeys(component, key) )
    {
        /* Update the specified key with the new value */
    }
    else
    {
        return GGL_ERR_FAILURE;
    }
}

GglError ggconfig_insertKeyAndValue(const char *key, const char *value, const char *component)
{
    makeConfigurationReady();
    /* create a new key on the keypath for this component */
    if( isKnownComponent(component))
    {

    }
    else
    {
        return GGL_ERR_FAILURE;
    }

}

GglError ggconfig_getValueFromKey(const char *key, const char *valueBuffer, size_t *valueBufferLength, const char *component )
{
    makeConfigurationReady();
    if( validateKeys( component, key ))
    {
        /* collect the data and write it to the supplied buffer. */
        /* if the valueBufferLength is too small, return GGL_ERR_FAILURE */
    }
    else
    {
        return GGL_ERR_FAILURE;
    }

}

GglError ggconfig_insertComponent(const char *component)
{
    makeConfigurationReady();
    if( isKnownComponent(component))
    {
        return GGL_ERR_FAILURE;
    }
    else
    {
        /* create the new component */
    }
}

GglError ggconfig_deleteComponent(const char *component)
{
    makeConfigurationReady();
    if( isKnownComponent(component) )
    {
        /* delete the component */
    }
    else
    {
        return GGL_ERR_FAILURE;
    }

}

GglError ggconfig_getKeyNotification(const char *key, const char *component, ggconfig_Callback_t callback, void *parameter)
{
    makeConfigurationReady();

}
