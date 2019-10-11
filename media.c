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

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#include <linux/videodev2.h>
#include <linux/media.h>

#include "common.h"
#include "media.h"
#include "tools.h"

#define MEDIA_PAD_FL_SOURCE		MEDIA_PAD_FLAG_INPUT
#define MEDIA_PAD_FL_SINK		MEDIA_PAD_FLAG_OUTPUT


// функции для работы с медиа-конвейером схожие с возможностями media-ctl

struct media_entity_pad *media_entity_remote_source(struct media_entity_pad *pad)
{
	unsigned int i;

	for (i = 0; i < pad->entity->num_links; ++i) {
		struct media_entity_link *link = &pad->entity->links[i];

		if (!(link->flags & MEDIA_LINK_FLAG_ENABLED))
			continue;

		if (link->source == pad)
			return link->sink;

		if (link->sink == pad)
			return link->source;
	}

	return NULL;
}

struct media_entity *media_get_entity_by_name(struct media_device *media,
					      const char *name)
{
	unsigned int i;

	for (i = 0; i < media->entities_count; ++i) {
		struct media_entity *entity = &media->entities[i];

		if (strcmp(entity->info.name, name) == 0)
			return entity;
	}

	return NULL;
}

struct media_entity *media_get_entity_by_id(struct media_device *media,
					    __u32 id)
{
	unsigned int i;

	for (i = 0; i < media->entities_count; ++i) {
		struct media_entity *entity = &media->entities[i];

		if (entity->info.id == id)
			return entity;
	}

	return NULL;
}

int media_setup_link(struct media_device *media,
		     struct media_entity_pad *source,
		     struct media_entity_pad *sink,
		     __u32 flags)
{
	struct media_entity_link *link;
	struct media_link_desc ulink;
	unsigned int i;
	int ret;

	for (i = 0; i < source->entity->num_links; i++) {
		link = &source->entity->links[i];

		if (link->source->entity == source->entity &&
		    link->source->index == source->index &&
		    link->sink->entity == sink->entity &&
		    link->sink->index == sink->index)
			break;
	}

	if (i == source->entity->num_links) {
		ERR("%s: Link not found\n", __func__);
		return -EINVAL;
	}

	/* source pad */
	ulink.source.entity = source->entity->info.id;
	ulink.source.index = source->index;
	ulink.source.flags = MEDIA_PAD_FL_SOURCE;

	/* sink pad */
	ulink.sink.entity = sink->entity->info.id;
	ulink.sink.index = sink->index;
	ulink.sink.flags = MEDIA_PAD_FL_SINK;

	ulink.flags = flags | (link->flags & MEDIA_LINK_FLAG_IMMUTABLE);

	ret = ioctl(media->fd, MEDIA_IOC_SETUP_LINK, &ulink);
	if (ret < 0) {
		ERR("%s: Unable to setup link (%s)\n", __func__,
			strerror(errno));
		return ret;
	}

	link->flags = flags;
	return 0;
}

int media_reset_links(struct media_device *media)
{
	unsigned int i, j;
	int ret;

	for (i = 0; i < media->entities_count; ++i) {
		struct media_entity *entity = &media->entities[i];

		for (j = 0; j < entity->num_links; j++) {
			struct media_entity_link *link = &entity->links[j];

			if (link->flags & MEDIA_LINK_FLAG_IMMUTABLE ||
			    link->source->entity != entity)
				continue;

			ret = media_setup_link(media, link->source, link->sink,
					       link->flags & ~MEDIA_LINK_FLAG_ENABLED);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static struct media_entity_link *media_entity_add_link(struct media_entity *entity)
{
	if(entity == NULL)
	{
		ERR("Dereferencing of the null pointer 'entity' \r\n");
		return NULL;
	}

	if (entity->num_links >= entity->max_links) {
		struct media_entity_link *links = entity->links;
		unsigned int max_links = entity->max_links * 2;
		unsigned int i;

		links = realloc(links, max_links * sizeof *links);
		if(links == NULL)
		{
			ERR("Dereferencing of the null pointer 'links' \r\n");
			return NULL;
		}

		for(i = 0; i < entity->num_links; ++i)
		{
			links[i].reverse->reverse = &links[i];
		}

		entity->max_links = max_links;
		entity->links = links;
	}

	return &entity->links[entity->num_links++];
}

static int media_enum_links(struct media_device *media)
{
	__u32 id;
	int ret = 0;

	for (id = 1; id <= media->entities_count; id++) {
		struct media_entity *entity = &media->entities[id - 1];
		struct media_links_enum links;
		unsigned int i;

		links.entity = entity->info.id;
		links.pads = calloc(entity->info.pads, sizeof(struct media_pad_desc));
		links.links = calloc(entity->info.links, sizeof(struct media_link_desc));

		if (links.pads == NULL || links.links == NULL)
			return -ENOMEM;

		if (ioctl(media->fd, MEDIA_IOC_ENUM_LINKS, &links) < 0) {
			ERR("%s: Unable to enumerate pads and links (%s).\n",
				__func__, strerror(errno));
			free(links.pads);
			free(links.links);
			return -errno;
		}

		for (i = 0; i < entity->info.pads; ++i) {
			entity->pads[i].entity = entity;
			entity->pads[i].index = links.pads[i].index;
			entity->pads[i].flags = links.pads[i].flags;
		}

		for (i = 0; i < entity->info.links; ++i) {
			struct media_link_desc *link = &links.links[i];
			struct media_entity_link *fwdlink;
			struct media_entity_link *backlink;
			struct media_entity *source;
			struct media_entity *sink;

			source = media_get_entity_by_id(media, link->source.entity);
			sink = media_get_entity_by_id(media, link->sink.entity);

			if (source == NULL || sink == NULL) {
				WARN("Entity %u link %u from %u/%u to %u/%u is invalid!\n",
					id, i, link->source.entity, link->source.index,
					link->sink.entity, link->sink.index);
				ret = -EINVAL;
			}

			fwdlink = media_entity_add_link(source);
			if(fwdlink == NULL)
			{
				ERR("Dereferencing of a null pointer 'fwdlink'\r\n");
				return -errno;	
			}
			fwdlink->source = &source->pads[link->source.index];
			fwdlink->sink = &sink->pads[link->sink.index];
			fwdlink->flags = links.links[i].flags;

			backlink = media_entity_add_link(sink);
			if(backlink == NULL)
			{
				ERR("Dereferencing of a null pointer 'backlink'\r\n");
				return -errno;	
			}
			backlink->source = &source->pads[link->source.index];
			backlink->sink = &sink->pads[link->sink.index];
			backlink->flags = links.links[i].flags;

			fwdlink->reverse = backlink;
			backlink->reverse = fwdlink;
		}

		free(links.pads);
		free(links.links);
	}

	return ret;
}

static int media_enum_entities(struct media_device *media)
{
	struct media_entity *entity;
	struct stat devstat;
	unsigned int size;
	char devname[32];
	char sysname[32];
	char target[1024];
	char *p;
	__u32 id;
	int ret;

	for (id = 0; ; id = entity->info.id) {
		size = (media->entities_count + 1) * sizeof(*media->entities);
		media->entities = realloc(media->entities, size);
		if(media->entities == NULL)
		{
			ERR("Original pointer 'media->entities' is lost\r\n");
			return -ENOMEM;
		}

		entity = &media->entities[media->entities_count];
		memset(entity, 0, sizeof(*entity));
		entity->fd = -1;
		entity->info.id = id | MEDIA_ENTITY_ID_FLAG_NEXT;

		ret = ioctl(media->fd, MEDIA_IOC_ENUM_ENTITIES, &entity->info);
		if (ret < 0) {
			if (errno == EINVAL)
				break;
			return -errno;
		}

		/* Number of links (for outbound links) plus number of pads (for
		 * inbound links) is a good safe initial estimate of the total
		 * number of links.
		 */
		entity->max_links = entity->info.pads + entity->info.links;

		entity->pads = malloc(entity->info.pads * sizeof(*entity->pads));
		entity->links = malloc(entity->max_links * sizeof(*entity->links));
		if (entity->pads == NULL || entity->links == NULL)
			return -ENOMEM;

		media->entities_count++;

		/* Find the corresponding device name. */
		if (media_entity_type(entity) != MEDIA_ENTITY_TYPE_DEVNODE &&
		    media_entity_type(entity) != MEDIA_ENTITY_TYPE_V4L2_SUBDEV)
			continue;

		sprintf(sysname, "/sys/dev/char/%u:%u", entity->info.v4l.major,
			entity->info.v4l.minor);
		ret = readlink(sysname, target, sizeof(target));
		if (ret < 0)
			continue;

		target[ret] = '\0';
		p = strrchr(target, '/');
		if (p == NULL)
			continue;

		sprintf(devname, "/dev/%s", p + 1);
		ret = stat(devname, &devstat);
		if (ret < 0)
			continue;

		/* Sanity check: udev might have reordered the device nodes.
		 * Make sure the major/minor match. We should really use
		 * libudev.
		 */
		if (major(devstat.st_rdev) == entity->info.v4l.major &&
		    minor(devstat.st_rdev) == entity->info.v4l.minor)
			strcpy(entity->devname, devname);
	}

	return 0;
}

struct media_device *media_open(const char *name, int verbose)
{
	struct media_device *media;
	int ret;

	media = malloc(sizeof(*media));
	if (media == NULL) {
		ERR("%s: unable to allocate memory\n", __func__);
		return NULL;
	}
	memset(media, 0, sizeof(*media));

	if (verbose)
		ERR("Opening media device %s\n", name);
	media->fd = open(name, O_RDWR);
	if (media->fd < 0) {
		media_close(media);
		ERR("%s: Can't open media device %s\n", __func__, name);
		return NULL;
	}

	ret = ioctl(media->fd, MEDIA_IOC_DEVICE_INFO, &media->info);
	if (ret < 0) {
		ERR("%s: Unable to retrieve media device information for "
		       "device %s (%s)\n", __func__, name, strerror(errno));
		media_close(media);
		return NULL;
	}

	if (verbose)
		ERR("Enumerating entities\n");

	ret = media_enum_entities(media);
	if (ret < 0) {
		ERR("%s: Unable to enumerate entities for device %s (%s)\n",
			__func__, name, strerror(-ret));
		media_close(media);
		return NULL;
	}

	if (verbose) {
		debug("Found %u entities\n", media->entities_count);
		debug("Enumerating pads and links\n");
	}

	ret = media_enum_links(media);
	if (ret < 0) {
		ERR("%s: Unable to enumerate pads and linksfor device %s\n",
			__func__, name);
		media_close(media);
		return NULL;
	}

	return media;
}

void media_close(struct media_device *media)
{
	unsigned int i;

	if(media == NULL)
	{
		return;
	}

	if (media->fd != -1)
		close(media->fd);

	for (i = 0; i < media->entities_count; ++i) {
		struct media_entity *entity = &media->entities[i];

		if(entity->pads)
		{
			free(entity->pads);
		}
		if(entity->links)
		{
			free(entity->links);
		}
		if (entity->fd != -1)
			close(entity->fd);
	}

	if(media->entities)
	{
		free(media->entities);
	}
	free(media);
}
