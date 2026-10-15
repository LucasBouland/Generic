#pragma once

/*
 * Forwarding header. The container world actor now lives in Item/Actor beside the other world item
 * actors; this path is kept so existing includes keep building. New code includes
 * "Item/Actor/PEPContainerItemWorldActor.h" directly. Nothing is declared here, so the reflected
 * class keeps its one header and its script path is unchanged: no redirect is needed.
 */
#include "Item/Actor/PEPContainerItemWorldActor.h"
