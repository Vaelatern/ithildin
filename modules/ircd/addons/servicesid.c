/*
 * servicesid.c: An ID number tagging facility used by various services
 * 
 * Copyright 2002 the Ithildin Project.
 * See the COPYING file for more information on licensing and use.
 * 
 * This module adds the data-holder for services id numbers.  It is up to other
 * consumers to deal with passing these id numbers around, however.
 */

#include <ithildin/stand.h>

#include "ircd.h"
#include "addons/servicesid.h"

IDSTRING(rcsid, "$Id: servicesid.c 579 2005-08-21 06:38:18Z wd $");

MODULE_REGISTER("$Rev: 579 $");

/*
@DEPENDENCIES@:        ircd
*/

struct mdext_item *servicesid_mdext;

MODULE_LOADER(servicesid) {

    if (!get_module_savedata(savelist, "servicesid_mdext", &servicesid_mdext))
        servicesid_mdext = create_mdext_item(ircd.mdext.client,
                sizeof(uint32_t));

    return 1;
}

MODULE_UNLOADER(servicesid) {
    
    if (reload)
        add_module_savedata(savelist, "servicesid_mdext",
                sizeof(servicesid_mdext), &servicesid_mdext);
    else
        destroy_mdext_item(ircd.mdext.client, servicesid_mdext);
}

/* vi:set ts=8 sts=4 sw=4 tw=76 et: */
