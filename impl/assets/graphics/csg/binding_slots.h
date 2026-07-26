// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_CSG_BINDING_SLOTS_H
#define NWB_GRAPHICS_CSG_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_CSG_CLIP_SET
#define NWB_CSG_CLIP_SET 1
#endif

// Historical clip SRVs at bindings 0/1 are now intentional ABI gaps. A single local cbuffer selects their
// persistent StorageBuffer heap entries (plus the cap-fill typed-material / mesh-instance entries) at binding 2.
#define NWB_CSG_BINDING_CLIP_CONTEXT_SLOTS 2

#ifndef NWB_CSG_INTERVAL_SET
#define NWB_CSG_INTERVAL_SET 0
#endif

#ifndef NWB_CSG_INTERVAL_SAMPLE_SET
#define NWB_CSG_INTERVAL_SAMPLE_SET 2
#endif

#define NWB_CSG_INTERVAL_BINDING_CAP_BACK_NORMAL 0
#define NWB_CSG_INTERVAL_BINDING_DEPTH 1
#define NWB_CSG_INTERVAL_BINDING_ID 2
#define NWB_CSG_INTERVAL_BINDING_RECEIVER_EVENT_DATA 5
#define NWB_CSG_INTERVAL_BINDING_RECEIVER_EVENT_COUNT 6
#define NWB_CSG_INTERVAL_BINDING_RECEIVER_SPAN_DATA 9
#define NWB_CSG_INTERVAL_BINDING_RECEIVER_SPAN_COUNT 10
#define NWB_CSG_INTERVAL_BINDING_REMOVED_INTERVAL_DEPTH 12
#define NWB_CSG_INTERVAL_BINDING_SAMPLE_STATE 18
#define NWB_CSG_INTERVAL_BINDING_REMOVED_INTERVAL_CAP_NORMAL 14
#define NWB_CSG_INTERVAL_BINDING_REMOVED_INTERVAL_DATA 15
#define NWB_CSG_INTERVAL_BINDING_REMOVED_INTERVAL_COUNT 16
// The CSG transient textures are global StorageImage heap entries. Keep their former local slots as intentional
// gaps and carry the target-generation DeferredBindlessResourceSlots cbuffer in the spare binding 17.
#define NWB_CSG_INTERVAL_BINDING_BINDLESS_RESOURCES 17


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

