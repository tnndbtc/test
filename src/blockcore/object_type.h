// ============= object_type.h =============
#ifndef OBJECT_TYPE_H
#define OBJECT_TYPE_H

/**
 * @file object_type.h
 * @brief Defines object types for P2P inventory messages
 *
 * Object types are used in inventory (INV) and GETDATA messages
 * to identify the type of data being announced or requested.
 */

namespace ObjectType {
    /**
     * @brief Object type identifiers
     *
     * Used in P2P protocol messages to identify the type of object
     * in inventory announcements and data requests.
     */
    typedef unsigned short int Type;

    constexpr Type OBJ_BEGIN = 0;    ///< Begin index of object definition
    constexpr Type BLOCK = 1;        ///< Block object
    constexpr Type TRANSACTION = 2;  ///< Transaction object
    constexpr Type OBJ_LAST = 3;     ///< Last index of object definition
}

#endif // OBJECT_TYPE_H
