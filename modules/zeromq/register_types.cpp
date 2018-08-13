
#include "class_db.h"
#include "register_types.h"
#include "zeromq_wrapper.h"

void register_zeromq_types() {
	ClassDB::register_class<Zeromq_wrapper>();
}

void unregister_zeromq_types() {

}
