/*
 * OMAP3 ISP library - Media controller
 *
 * Copyright (C) 2010-2011 Ideas on board SPRL
 *
 * Contact: Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef __MEDIA_H__
#define __MEDIA_H__

#include <linux/media.h>

struct media_entity_link {
	struct media_entity_pad *source;
	struct media_entity_pad *sink;
	struct media_entity_link *reverse;
	__u32 flags;
};

struct media_entity_pad {
	struct media_entity *entity;
	__u32 index;
	__u32 flags;
};

struct media_entity {
	struct media_entity_desc info;
	struct media_entity_pad *pads;
	struct media_entity_link *links;
	unsigned int max_links;
	unsigned int num_links;

	char devname[32];
	int fd;
};

struct media_device {
	int fd;
	struct media_device_info info;
	struct media_entity *entities;
	unsigned int entities_count;
};

/*
 * media_open - Open a media device
 * @name: Name (including path) of the device node
 * @verbose: Whether to print verbose information on the standard output
 *
 * Open the media device referenced by @name and enumerate entities, pads and
 * links.
 *
 * Return a pointer to a newly allocated media_device structure instance on
 * success and NULL on failure. The returned pointer must be freed with
 * media_close when the device isn't needed anymore.
 */
struct media_device *media_open(const char *name, int verbose);

/*
 * media_close - Close a media device
 * @media: Device instance
 *
 * Close the @media device instance and free allocated resources. Access to the
 * device instance is forbidden after this function returns.
 */
void media_close(struct media_device *media);

/*
 * media_entity_remote_source - Locate the pad at the other end of a link
 * @pad: Sink pad at one end of the link
 *
 * Locate the source pad connected to @pad through an enabled link. As only one
 * link connected to a sink pad can be enabled at a time, the connected source
 * pad is guaranteed to be unique.
 *
 * Return a pointer to the connected source pad, or NULL if all links connected
 * to @pad are disabled.
 */
struct media_entity_pad *media_entity_remote_source(struct media_entity_pad *pad);

/*
 * media_entity_type - Get the type of an entity
 * @entity: The entity
 *
 * Return the type of @entity.
 */
static inline unsigned int media_entity_type(struct media_entity *entity)
{
	return entity->info.type & MEDIA_ENTITY_TYPE_MASK;
}

/*
 * media_get_entity_by_name - Find an entity by its name
 * @media: Media device
 * @name: Entity name
 *
 * Search for an entity with a name equal to @name. Return a pointer to the
 * entity if found, or NULL otherwise.
 */
struct media_entity *media_get_entity_by_name(struct media_device *media,
	const char *name);

/*
 * media_get_entity_by_id - Find an entity by its ID
 * @media: Media device
 * @id: Entity ID
 *
 * Search for an entity with an ID equal to @id. Return a pointer to the entity
 * if found, or NULL otherwise.
 */
struct media_entity *media_get_entity_by_id(struct media_device *media,
	__u32 id);

/*
 * media_setup_link - Configure a link
 * @media: Media device
 * @source: Source pad at the link origin
 * @sink: Sink pad at the link target
 * @flags: Configuration flags
 *
 * Locate the link between @source and @sink, and configure it by applying the
 * new @flags.
 *
 * Only the MEDIA_LINK_FLAG_ENABLED flag is writable.
 *
 * Return 0 on success, or a negative error code on failure.
 */
int media_setup_link(struct media_device *media,
	struct media_entity_pad *source, struct media_entity_pad *sink,
	__u32 flags);

/*
 * media_reset_links - Reset all links to the disabled state.
 * @media: Media device
 *
 * Disable all links in the media device. This function is usually used after
 * opening a media device to reset all links to a known state.
 * 
 * Return 0 on success, or a negative error code on failure.
 */
int media_reset_links(struct media_device *media);

#endif
