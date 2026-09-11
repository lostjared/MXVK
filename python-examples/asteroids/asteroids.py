#!/usr/bin/env python3

import argparse
import math
import random
import time
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

import mxvk_ext as mxvk


GAME_W = 640
GAME_H = 360
STAR_COUNT = 800
MAX_PROJECTILES = 50
MAX_ASTEROIDS = 30
MAX_PARTICLES = 40
ASTEROID_VERTICES = 8
PROJECTILE_SPEED = 5.0
PROJECTILE_LIFETIME = 60
FIRE_COOLDOWN = 5
SHOTS_PER_BURST = 5
FIRE_DELAY = 3
EXPLOSION_DURATION = 90
MIN_ASTEROID_RADIUS = 8.0
LARGE_ASTEROID_POINTS = 20
MEDIUM_ASTEROID_POINTS = 50
SMALL_ASTEROID_POINTS = 100
TARGET_FRAME_TIME = 1.0 / 60.0

EXAMPLE_DIR = Path(__file__).resolve().parent
DATA_DIR = EXAMPLE_DIR / "data"


@dataclass
class Ship:
    x: float = GAME_W / 2
    y: float = GAME_H / 2
    vx: float = 0.0
    vy: float = 0.0
    angle: float = 0.0
    lives: int = 3
    fire_cooldown: int = 0
    burst_count: int = 0
    exploding: bool = False
    explosion_timer: int = 0
    score: int = 0
    continuous_fire_timer: int = 0
    overheated: bool = False
    overheat_cooldown: int = 0


@dataclass
class Projectile:
    x: float = 0.0
    y: float = 0.0
    vx: float = 0.0
    vy: float = 0.0
    lifetime: int = 0
    active: bool = False


@dataclass
class Asteroid:
    x: float = 0.0
    y: float = 0.0
    vx: float = 0.0
    vy: float = 0.0
    radius: float = 0.0
    active: bool = False
    vertices: list[tuple[float, float]] = field(default_factory=list)
    rotation_angle: float = 0.0
    rotation_speed: float = 0.0


@dataclass
class Particle:
    x: float = 0.0
    y: float = 0.0
    vx: float = 0.0
    vy: float = 0.0
    lifetime: int = 0
    active: bool = False


@dataclass
class Star:
    x: float
    y: float
    speed: float
    size: float
    brightness: float
    layer: int
    red: float = 1.0
    green: float = 1.0
    blue: float = 1.0
    twinkle_phase: float = 0.0
    twinkle_speed: float = 1.0


class AsteroidsWindow(mxvk.VK_Window):
    def __init__(self, width: int, height: int, fullscreen: bool, enable_vsync: bool) -> None:
        super().__init__("-[ SpaceRox - MXVK ]-", width, height, fullscreen, False, enable_vsync)
        self.fallback_width = width
        self.fallback_height = height
        self.width = width
        self.height = height
        self.last_font_size = 0
        self.last_frame_time = time.monotonic()
        self.state = "countdown"
        self.state_started = self.last_frame_time
        self.key_left = False
        self.key_right = False
        self.key_thrust = False
        self.key_fire = False
        self.was_exploding = False
        self.ship = Ship()
        self.projectiles = [Projectile() for _ in range(MAX_PROJECTILES)]
        self.asteroids = [Asteroid() for _ in range(MAX_ASTEROIDS)]
        self.particles = [Particle() for _ in range(MAX_PARTICLES)]
        self.stars: list[Star] = []
        self.set_font(str(DATA_DIR / "font.ttf"), 20)
        self.star_sprite = self.create_sprite(4, 4, str(DATA_DIR / "sprite_vert.spv"), str(DATA_DIR / "sprite_frag.spv"))
        self.pixel = self.create_sprite(2, 2, str(DATA_DIR / "sprite_vert.spv"), str(DATA_DIR / "solid_frag.spv"))
        self.star_sprite.update_texture(self.make_star_pixels(), 4, 4, 16)
        self.pixel.update_texture(np.full((2, 2, 4), 255, dtype=np.uint8), 2, 2, 8)
        self.init_game()

    def close(self) -> None:
        self.star_sprite = None
        self.pixel = None
        self.release()

    def make_star_pixels(self) -> np.ndarray:
        pixels = np.zeros((4, 4, 4), dtype=np.uint8)
        for y in range(4):
            for x in range(4):
                distance = math.hypot((x - 1.5) / 1.5, (y - 1.5) / 1.5)
                pixels[y, x] = (255, 255, 255, max(0, int(255 * (1.0 - distance))))
        return pixels

    def sx(self) -> float:
        return self.width / GAME_W

    def sy(self) -> float:
        return self.height / GAME_H

    def to_x(self, value: float) -> int:
        return int(value * self.sx())

    def to_y(self, value: float) -> int:
        return int(value * self.sy())

    def to_w(self, value: float) -> int:
        return max(1, int(value * self.sx()))

    def to_h(self, value: float) -> int:
        return max(1, int(value * self.sy()))

    def refresh_size(self) -> None:
        width, height = self.swapchain_extent
        self.width = width if width > 0 else self.fallback_width
        self.height = height if height > 0 else self.fallback_height

    def update_font_size(self) -> None:
        font_size = max(14, min(128, int(20.0 * self.height / 480.0)))
        if font_size != self.last_font_size:
            self.last_font_size = font_size
            self.set_font(str(DATA_DIR / "font.ttf"), font_size)
            self.clear_text_queue()

    def init_game(self) -> None:
        self.ship = Ship()
        self.projectiles = [Projectile() for _ in range(MAX_PROJECTILES)]
        self.asteroids = [Asteroid() for _ in range(MAX_ASTEROIDS)]
        self.particles = [Particle() for _ in range(MAX_PARTICLES)]
        for _ in range(4):
            x, y = self.random_safe_position()
            self.spawn_asteroid(x, y, random.uniform(-1.0, 1.0), random.uniform(-1.0, 1.0), random.uniform(20.0, 39.0))
        self.init_stars()
        self.state = "countdown"
        self.state_started = time.monotonic()
        self.was_exploding = False

    def random_safe_position(self) -> tuple[float, float]:
        while True:
            x, y = random.uniform(0, GAME_W), random.uniform(0, GAME_H)
            if math.hypot(x - self.ship.x, y - self.ship.y) >= 100:
                return x, y

    def init_stars(self) -> None:
        self.stars = []
        for layer in range(3):
            for _ in range(STAR_COUNT):
                speed_range = ((0.15, 0.55), (0.4, 1.2), (0.8, 2.6))[layer]
                size_range = ((0.8, 1.4), (1.2, 2.1), (1.8, 3.05))[layer]
                brightness_range = ((0.25, 0.7), (0.45, 1.0), (0.65, 1.0))[layer]
                star = Star(random.uniform(0, GAME_W), random.uniform(0, GAME_H), random.uniform(*speed_range), random.uniform(*size_range), random.uniform(*brightness_range), layer, twinkle_phase=random.uniform(0, math.tau), twinkle_speed=random.uniform(0.4 + layer * 0.4, 1.9 + layer * 1.5))
                self.assign_star_color(star)
                self.stars.append(star)

    def assign_star_color(self, star: Star) -> None:
        roll = random.random()
        if roll < 0.35:
            star.red, star.green, star.blue = random.uniform(0.85, 1.0), random.uniform(0.9, 1.0), 1.0
        elif roll < 0.55:
            star.red, star.green, star.blue, star.size, star.brightness = random.uniform(0.5, 0.7), random.uniform(0.75, 0.9), 1.0, star.size * 1.15, star.brightness * 1.1
        elif roll < 0.70:
            star.red, star.green, star.blue, star.size = 1.0, random.uniform(0.95, 1.0), random.uniform(0.6, 0.9), star.size * 1.05
        elif roll < 0.90:
            star.red, star.green, star.blue, star.size = 1.0, random.uniform(0.3, 0.8), random.uniform(0.2, 0.4), star.size * 1.1
        else:
            star.red, star.green, star.blue, star.size, star.brightness = 0.9, 0.95, 1.0, star.size * 1.5, star.brightness * 1.4

    def set_color(self, red: float, green: float, blue: float, alpha: float = 1.0) -> None:
        self.pixel.set_shader_params(red, green, blue, alpha)

    def draw_rect(self, x: float, y: float, width: float, height: float) -> None:
        self.pixel.draw_rect(self.to_x(x), self.to_y(y), self.to_w(width), self.to_h(height))

    def draw_line(self, x0: float, y0: float, x1: float, y1: float) -> None:
        x0, y0, x1, y1 = self.to_x(x0), self.to_y(y0), self.to_x(x1), self.to_y(y1)
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        step_x, step_y = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        error = dx + dy
        size = max(1, int(1.5 * min(self.sx(), self.sy())))
        while True:
            self.pixel.draw_rect(x0, y0, size, size)
            if x0 == x1 and y0 == y1:
                return
            twice_error = 2 * error
            if twice_error >= dy:
                error += dy
                x0 += step_x
            if twice_error <= dx:
                error += dx
                y0 += step_y

    def spawn_asteroid(self, x: float, y: float, vx: float, vy: float, radius: float) -> None:
        asteroid = next((value for value in self.asteroids if not value.active), None)
        if asteroid is None:
            return
        asteroid.x, asteroid.y, asteroid.vx, asteroid.vy, asteroid.radius, asteroid.active = x, y, vx, vy, radius, True
        asteroid.rotation_angle, asteroid.rotation_speed = 0.0, random.uniform(-0.05, 0.05)
        asteroid.vertices = []
        for index in range(ASTEROID_VERTICES):
            angle = index * math.tau / ASTEROID_VERTICES
            vertex_radius = radius * random.uniform(0.7, 0.99)
            asteroid.vertices.append((math.cos(angle) * vertex_radius, math.sin(angle) * vertex_radius))

    def update_ship(self) -> None:
        if self.ship.exploding:
            return
        if self.key_left:
            self.ship.angle -= 0.15
        if self.key_right:
            self.ship.angle += 0.15
        if self.key_thrust:
            self.ship.vx += math.sin(self.ship.angle) * 0.2
            self.ship.vy -= math.cos(self.ship.angle) * 0.2
            speed = math.hypot(self.ship.vx, self.ship.vy)
            if speed > 4.0:
                self.ship.vx, self.ship.vy = self.ship.vx * 4.0 / speed, self.ship.vy * 4.0 / speed
        self.ship.vx *= 0.98
        self.ship.vy *= 0.98
        self.ship.x = (self.ship.x + self.ship.vx) % GAME_W
        self.ship.y = (self.ship.y + self.ship.vy) % GAME_H
        self.ship.fire_cooldown = max(0, self.ship.fire_cooldown - 1)

    def can_fire(self) -> bool:
        if self.ship.overheated or self.ship.fire_cooldown > 0:
            return False
        if self.ship.burst_count >= SHOTS_PER_BURST:
            self.ship.fire_cooldown, self.ship.burst_count = FIRE_COOLDOWN, 0
            return False
        self.ship.fire_cooldown += FIRE_DELAY
        self.ship.burst_count += 1
        return True

    def fire_projectile(self) -> None:
        projectile = next((value for value in self.projectiles if not value.active), None)
        if projectile is None:
            return
        projectile.x = self.ship.x + math.sin(self.ship.angle) * 12
        projectile.y = self.ship.y - math.cos(self.ship.angle) * 12
        projectile.vx = math.sin(self.ship.angle) * PROJECTILE_SPEED
        projectile.vy = -math.cos(self.ship.angle) * PROJECTILE_SPEED
        projectile.lifetime, projectile.active = PROJECTILE_LIFETIME, True

    def update_fire(self) -> None:
        if self.key_fire and self.can_fire():
            self.fire_projectile()
        if self.key_fire and not self.ship.overheated:
            self.ship.continuous_fire_timer += 1
            if self.ship.continuous_fire_timer >= 180:
                self.ship.overheated, self.ship.continuous_fire_timer, self.ship.burst_count = True, 0, 0
        elif self.ship.overheated:
            self.ship.overheat_cooldown += 1
            if self.ship.overheat_cooldown >= 180:
                self.ship.overheated, self.ship.overheat_cooldown = False, 0
        else:
            self.ship.continuous_fire_timer = max(0, self.ship.continuous_fire_timer - 1)

    def update_projectiles(self) -> None:
        for projectile in self.projectiles:
            if projectile.active:
                projectile.x += projectile.vx
                projectile.y += projectile.vy
                projectile.lifetime -= 1
                if projectile.lifetime <= 0 or not (0 <= projectile.x < GAME_W and 0 <= projectile.y < GAME_H):
                    projectile.active = False

    def update_asteroids(self) -> None:
        for asteroid in self.asteroids:
            if asteroid.active:
                asteroid.x = (asteroid.x + asteroid.vx) % GAME_W
                asteroid.y = (asteroid.y + asteroid.vy) % GAME_H
                asteroid.rotation_angle += asteroid.rotation_speed

    def split_asteroid(self, asteroid: Asteroid) -> None:
        radius = asteroid.radius / 2.0
        if radius < MIN_ASTEROID_RADIUS:
            self.create_explosion(asteroid.x, asteroid.y)
            self.ship.score += SMALL_ASTEROID_POINTS
            asteroid.active = False
            return
        for _ in range(random.randint(2, 3)):
            angle, speed = random.uniform(0, math.tau), random.uniform(0.5, 1.9)
            self.spawn_asteroid(asteroid.x + math.cos(angle) * radius * 0.5, asteroid.y + math.sin(angle) * radius * 0.5, math.cos(angle) * speed + asteroid.vx * 0.3, math.sin(angle) * speed + asteroid.vy * 0.3, radius)
        self.ship.score += LARGE_ASTEROID_POINTS if radius >= 25.0 else MEDIUM_ASTEROID_POINTS
        self.create_explosion(asteroid.x, asteroid.y)
        asteroid.active = False

    def create_explosion(self, x: float, y: float) -> None:
        for particle in [value for value in self.particles if not value.active][:random.randint(15, 24)]:
            angle, speed = random.uniform(0, math.tau), random.uniform(0.5, 3.0)
            particle.x, particle.y, particle.vx, particle.vy, particle.lifetime, particle.active = x + random.uniform(-8, 8), y + random.uniform(-8, 8), math.cos(angle) * speed, math.sin(angle) * speed, random.randint(20, 60), True

    def update_particles(self) -> None:
        for particle in self.particles:
            if particle.active:
                particle.x = (particle.x + particle.vx) % GAME_W
                particle.y = (particle.y + particle.vy) % GAME_H
                particle.vx *= 0.97
                particle.vy *= 0.97
                particle.lifetime -= 1
                if particle.lifetime <= 0:
                    particle.active = False

    def check_collisions(self) -> None:
        self.check_asteroid_collisions()
        for projectile in self.projectiles:
            if not projectile.active:
                continue
            for asteroid in self.asteroids:
                if asteroid.active and math.hypot(projectile.x - asteroid.x, projectile.y - asteroid.y) < asteroid.radius:
                    projectile.active = False
                    if asteroid.radius >= 20.0:
                        self.ship.score += LARGE_ASTEROID_POINTS
                    elif asteroid.radius >= MIN_ASTEROID_RADIUS:
                        self.ship.score += MEDIUM_ASTEROID_POINTS
                    else:
                        self.ship.score += SMALL_ASTEROID_POINTS
                    self.split_asteroid(asteroid)
                    break
        if self.ship.exploding:
            return
        for asteroid in self.asteroids:
            if asteroid.active and math.hypot(self.ship.x - asteroid.x, self.ship.y - asteroid.y) < asteroid.radius + 8:
                self.ship.exploding, self.ship.explosion_timer, self.ship.lives = True, EXPLOSION_DURATION, self.ship.lives - 1
                self.create_explosion(self.ship.x, self.ship.y)
                break

    def check_asteroid_collisions(self) -> None:
        active_asteroids = [asteroid for asteroid in self.asteroids if asteroid.active]
        for index, first in enumerate(active_asteroids):
            for second in active_asteroids[index + 1:]:
                difference_x, difference_y = first.x - second.x, first.y - second.y
                distance = math.hypot(difference_x, difference_y)
                minimum_distance = first.radius + second.radius
                if distance <= 0.0 or distance >= minimum_distance:
                    continue
                normal_x, normal_y = difference_x / distance, difference_y / distance
                overlap = (minimum_distance - distance) * 0.5
                first.x, first.y = first.x + normal_x * overlap, first.y + normal_y * overlap
                second.x, second.y = second.x - normal_x * overlap, second.y - normal_y * overlap
                velocity_along_normal = (first.vx - second.vx) * normal_x + (first.vy - second.vy) * normal_y
                if velocity_along_normal > 0.0:
                    continue
                impulse = -(1.0 + 0.8) * velocity_along_normal * 0.5
                first.vx, first.vy = first.vx + impulse * normal_x, first.vy + impulse * normal_y
                second.vx, second.vy = second.vx - impulse * normal_x, second.vy - impulse * normal_y

    def maintain_asteroids(self) -> None:
        while sum(value.active for value in self.asteroids) < 3:
            edge = random.randrange(4)
            x, y = ((-30, random.uniform(0, GAME_H)), (GAME_W + 30, random.uniform(0, GAME_H)), (random.uniform(0, GAME_W), -30), (random.uniform(0, GAME_W), GAME_H + 30))[edge]
            self.spawn_asteroid(x, y, random.uniform(-1, 1), random.uniform(-1, 1), random.choice((random.uniform(12, 20), random.uniform(20, 30), random.uniform(30, 45))))

    def update_death(self) -> None:
        if self.ship.exploding:
            self.ship.explosion_timer -= 1
            self.was_exploding = True
            if self.ship.explosion_timer <= 0:
                self.ship.exploding = False
        elif self.was_exploding:
            self.was_exploding = False
            if self.ship.lives <= 0:
                self.state = "gameover"
            else:
                self.ship.x, self.ship.y, self.ship.vx, self.ship.vy, self.ship.angle = GAME_W / 2, GAME_H / 2, 0.0, 0.0, 0.0
                self.key_left, self.key_right, self.key_thrust, self.key_fire = False, False, False, False
                self.state, self.state_started = "countdown", time.monotonic()

    def draw_stars(self, speed_multiplier: float) -> None:
        for star in self.stars:
            star.y += star.speed * speed_multiplier
            if star.y >= GAME_H:
                star.x, star.y = random.uniform(0, GAME_W), 0.0
            star.twinkle_phase += star.twinkle_speed / 60.0
            brightness = star.brightness * (0.7 + 0.3 * math.sin(star.twinkle_phase))
            self.star_sprite.set_shader_params(star.red * brightness, star.green * brightness, star.blue * brightness, brightness)
            size = max(2, int(star.size * min(self.sx(), self.sy())))
            self.star_sprite.draw_rect(self.to_x(star.x), self.to_y(star.y), size, size)

    def draw_ship(self, x: float, y: float) -> None:
        if self.ship.exploding:
            return
        cosine, sine = math.cos(self.ship.angle), math.sin(self.ship.angle)
        def rotate(local_x: float, local_y: float) -> tuple[float, float]: return x + local_x * cosine - local_y * sine, y + local_x * sine + local_y * cosine
        nose, left, right, left_engine, right_engine, center = rotate(0, -15), rotate(-12, 8), rotate(12, 8), rotate(-8, 12), rotate(8, 12), rotate(0, 10)
        if self.ship.overheated:
            self.set_color(1.0, 0.0, 0.0)
        else:
            self.set_color(1.0, 1.0, 1.0)
        for start, end in ((nose, left), (nose, right), (left, right), (left, left_engine), (right, right_engine), (left_engine, center), (right_engine, center)):
            self.draw_line(*start, *end)
        self.set_color(0.0, 1.0, 1.0)
        cockpit_left, cockpit_right, cockpit_center = rotate(-3, -8), rotate(3, -8), rotate(0, -5)
        self.draw_line(*cockpit_left, *cockpit_right)
        self.draw_line(*cockpit_left, *cockpit_center)
        self.draw_line(*cockpit_right, *cockpit_center)
        if self.key_thrust:
            self.set_color(1.0, 0.4, 0.0)
            for start, end in ((left_engine, rotate(-6, 18)), (center, rotate(0, 22)), (right_engine, rotate(6, 18))):
                self.draw_line(*start, *end)

    def draw_asteroids(self) -> None:
        for asteroid in self.asteroids:
            if not asteroid.active:
                continue
            cosine, sine = math.cos(asteroid.rotation_angle), math.sin(asteroid.rotation_angle)
            vertices = [(asteroid.x + x * cosine - y * sine, asteroid.y + x * sine + y * cosine) for x, y in asteroid.vertices]
            self.set_color(1.0, 1.0, 1.0)
            for index, vertex in enumerate(vertices):
                self.draw_line(*vertex, *vertices[(index + 1) % len(vertices)])
            inner = [(asteroid.x + (x - asteroid.x) * 0.6, asteroid.y + (y - asteroid.y) * 0.6) for x, y in vertices]
            self.set_color(0.59, 0.59, 0.59)
            for index, vertex in enumerate(inner):
                self.draw_line(*vertex, *inner[(index + 1) % len(inner)])
            self.set_color(0.39, 0.39, 0.39)
            for index in range(0, ASTEROID_VERTICES, 2):
                self.draw_line(*vertices[index], *inner[index])
            self.set_color(0.71, 0.71, 0.71)
            for index in range(0, ASTEROID_VERTICES, 2):
                self.draw_line(*vertices[index], *inner[(index + 1) % ASTEROID_VERTICES])

    def draw_particles(self) -> None:
        for particle in self.particles:
            if particle.active:
                ratio = particle.lifetime / 60.0
                self.set_color(1.0, min(1.0, ratio * 2.0), 0.0 if ratio < 0.5 else 0.3)
                self.draw_rect(particle.x, particle.y, 2, 2)

    def print_centered(self, text: str, y: int, color: mxvk.Color) -> None:
        dimensions = self.get_text_dimensions(text)
        width = dimensions[0] if dimensions else len(text) * self.last_font_size // 2
        self.print_text(text, self.width // 2 - width // 2, y, color)

    def draw_countdown(self, elapsed: float) -> None:
        self.draw_stars(0.3)
        number = 3 - int(elapsed)
        if number > 0:
            self.print_centered(str(number), self.height // 2, mxvk.Color(255, 255, 0))
        else:
            self.print_centered("LAUNCH!", self.height // 2, mxvk.Color(0, 255, 0))
        self.print_centered("PREPARE FOR MISSION", self.height // 2 + self.last_font_size * 2, mxvk.Color(255, 255, 255))

    def draw_game(self) -> None:
        self.draw_stars(1.0)
        self.draw_ship(self.ship.x, self.ship.y)
        self.set_color(1.0, 0.0, 0.0)
        for projectile in self.projectiles:
            if projectile.active:
                self.draw_line(projectile.x, projectile.y, projectile.x - projectile.vx * 0.5, projectile.y - projectile.vy * 0.5)
        self.draw_asteroids()
        self.draw_particles()
        self.print_text(f"Score: {self.ship.score}", self.to_x(10), self.to_y(10), mxvk.Color(255, 255, 255))
        self.print_text(f"Lives: {self.ship.lives}", self.to_x(10), self.to_y(30), mxvk.Color(255, 255, 255))

    def proc(self) -> None:
        current_time = time.monotonic()
        frame_time = current_time - self.last_frame_time
        if frame_time < TARGET_FRAME_TIME:
            time.sleep(TARGET_FRAME_TIME - frame_time)
            current_time = time.monotonic()
        self.last_frame_time = current_time
        self.refresh_size()
        self.update_font_size()
        now = current_time
        elapsed = now - self.state_started
        if self.state == "countdown":
            self.draw_countdown(elapsed)
            if elapsed >= 4.0:
                self.state, self.state_started = "launch", now
        elif self.state == "launch":
            self.draw_stars(0.5)
            launch_y = GAME_H - min(1.0, elapsed * 2.0) * (GAME_H - self.ship.y)
            self.draw_asteroids()
            self.draw_ship(self.ship.x, launch_y)
            if elapsed >= 0.5:
                self.print_centered("MISSION START!", self.height // 2, mxvk.Color(0, 255, 255))
            if elapsed >= 1.0:
                self.state = "playing"
        elif self.state == "playing":
            self.update_ship()
            self.update_fire()
            self.update_projectiles()
            self.update_asteroids()
            self.update_particles()
            self.check_collisions()
            self.maintain_asteroids()
            self.update_death()
            self.draw_game()
        else:
            self.draw_stars(0.2)
            self.print_centered("GAME OVER", self.to_y(130), mxvk.Color(255, 0, 0))
            self.print_centered(f"Final Score: {self.ship.score}", self.to_y(165), mxvk.Color(255, 255, 255))
            self.print_centered("Press SPACE to begin", self.to_y(195), mxvk.Color(255, 255, 0))

    def event(self, event: mxvk.Event) -> None:
        if event.key_down:
            if event.key == mxvk.KEY_ESCAPE:
                self.request_exit()
            elif self.state == "gameover" and event.key == mxvk.key_code("Space"):
                self.init_game()
            elif event.key == mxvk.key_code("Left"):
                self.key_left = True
            elif event.key == mxvk.key_code("Right"):
                self.key_right = True
            elif event.key == mxvk.key_code("Up"):
                self.key_thrust = True
            elif event.key == mxvk.key_code("Space"):
                self.key_fire = True
        elif event.key_up:
            if event.key == mxvk.key_code("Left"):
                self.key_left = False
            elif event.key == mxvk.key_code("Right"):
                self.key_right = False
            elif event.key == mxvk.key_code("Up"):
                self.key_thrust = False
            elif event.key == mxvk.key_code("Space"):
                self.key_fire = False


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="MXVK Python SpaceRox/Asteroids example")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--fullscreen", action="store_true")
    parser.add_argument("--vsync", action="store_true")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    window = AsteroidsWindow(args.width, args.height, args.fullscreen, args.vsync)
    try:
        window.loop()
    finally:
        window.close()


if __name__ == "__main__":
    main()
