"""Shared image-region and background pixel measurements."""


class ImageRegions:
    @staticmethod
    def estimate_background_rgb(rows_rgb, width, height):
        samples = []
        edge = min(width, height, max(min(width, height) // 24, 8), 32)
        step = max(min(width, height) // 128, 1)

        for y in range(0, edge, step):
            samples.extend(rows_rgb[y][::step])
        for y in range(max(height - edge, 0), height, step):
            samples.extend(rows_rgb[y][::step])
        for row in rows_rgb[::step]:
            samples.extend(row[:edge:step])
            samples.extend(row[max(width - edge, 0):width:step])

        if not samples:
            return (0, 0, 0)

        return tuple(sorted(channel)[len(samples) // 2] for channel in zip(*samples))


    @staticmethod
    def normalized_region(width, height, x0, y0, x1, y1):
        start_x = min(max(int(width * x0), 0), width)
        end_x = min(max(int(width * x1), start_x), width)
        start_y = min(max(int(height * y0), 0), height)
        end_y = min(max(int(height * y1), start_y), height)
        return start_x, start_y, end_x, end_y


    @staticmethod
    def region_pixel_count(region):
        start_x, start_y, end_x, end_y = region
        return max(end_x - start_x, 0) * max(end_y - start_y, 0)


    @staticmethod
    def count_foreground_pixels(rows_rgb, width, height, background, x0, y0, x1, y1):
        region = ImageRegions.normalized_region(width, height, x0, y0, x1, y1)
        return ImageRegions.count_foreground_pixels_in_region(rows_rgb, background, region)


    @staticmethod
    def count_foreground_pixels_in_region(rows_rgb, background, region, pixel_predicate=None):
        start_x, start_y, end_x, end_y = region
        count = 0

        for y in range(start_y, end_y):
            for x in range(start_x, end_x):
                red, green, blue = rows_rgb[y][x]
                channel_delta = (abs(red - background[0]), abs(green - background[1]), abs(blue - background[2]),)
                if sum(channel_delta) >= 34 and max(channel_delta) >= 14:
                    if pixel_predicate is None or pixel_predicate((red, green, blue)):
                        count += 1

        return count


    @staticmethod
    def count_background_like_pixels(rows_rgb, background, region):
        start_x, start_y, end_x, end_y = region
        count = 0

        for y in range(start_y, end_y):
            for x in range(start_x, end_x):
                red, green, blue = rows_rgb[y][x]
                channel_delta = (abs(red - background[0]), abs(green - background[1]), abs(blue - background[2]),)
                if sum(channel_delta) <= 28 and max(channel_delta) <= 12:
                    count += 1

        return count


