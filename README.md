# Sopwith — Meresyev Edition

[English version](#english) · [Оригинальный README SDL Sopwith](README-UPSTREAM.md)

**Sopwith — Meresyev Edition** — расширенная версия классической игры SDL
Sopwith. Теперь потеря самолёта не всегда означает потерю жизни: выживший пилот
может продолжить бой пешком, добраться до своей базы, использовать наземную
технику и снова подняться в воздух.

Проект основан на [SDL Sopwith](https://github.com/fragglet/sdl-sopwith) —
современном порте игры Sopwith Дэвида Л. Кларка, первоначально выпущенной BMB
Compuscience Canada. Исходный текст README сохранён в
[README-UPSTREAM.md](README-UPSTREAM.md).

## Основные возможности

- Пилот покидает разбившийся самолёт и может ходить, прыгать и стрелять.
- Пять жизней пилота; после гибели он возвращается на базу в новом самолёте.
- Ручные гранаты, выпадающие с каждого уничтоженного вражеского солдата; пилот
  начинает игру с двумя гранатами.
- Союзная и вражеская пехота с локальной зоной обнаружения, огнестрельным
  оружием и гранатами.
- Управляемые автомобили и танки. Танки сражаются с пехотой, техникой,
  самолётами и зданиями.
- Самолёты атакуют наземные цели пулемётами и бомбами; падающий самолёт также
  остаётся опасным оружием.
- Звуки стрельбы, гранат, техники и двигателя самолёта.
- Неограниченное число самолётов при сохранении ограничения на жизни пилота.

## Режимы игры

- **Vanilla — Novice / Expert** — классическая игра на стандартной карте с
  максимально сохранёнными правилами оригинала.
- **Random Map — Novice / Expert** — процедурные карты с разнообразным
  рельефом, зданиями, армиями и техникой.
- **Battlefield — Novice / Expert** — две армии ведут непрерывный бой за
  территорию. Подкрепления появляются примерно раз в минуту, разрушенные
  здания могут быть восстановлены пехотинцем за 40 секунд, а партия заканчивается
  после потери одной из сторон всей территории. Жизни игрока в этом режиме
  бесконечны.

ИИ и правила оригинальных режимов отделены от логики Battlefield.

## Управление по умолчанию

Клавиши можно переназначить в меню настроек.

| Действие | Клавиша |
|---|---|
| Поднять нос / движение | `,` |
| Опустить нос / движение | `/` |
| Переворот самолёта / прыжок / сесть в технику или выйти | `.` |
| Пулемёт / личное оружие / орудие танка | `Space` |
| Бомба / граната | `B` |
| Увеличить скорость | `X` |
| Уменьшить скорость | `Z` |
| Автопилот на базу | `H` |
| Звук | `S` |

## Готовые сборки

В [GitHub Releases](https://github.com/Jiablero/sopwith-meresyev-edition/releases)
публикуются сборки для:

- Windows x86_64;
- Linux x86_64 (архив и AppImage);
- Calculinux ARMv7 для PicoCalc + Luckfox Lyra.

Версия для PicoCalc использует экран 480×480. Исходное игровое поле 320×200
масштабируется без искажения пропорций до 480×300 и располагается по центру;
верхняя граница полётной области отмечена контрастной линией.

## Установка SDL2

- **Windows:** SDL2 уже находится в ZIP рядом с `sopwith.exe`; отдельно ничего
  устанавливать не нужно.
- **Linux AppImage:** необходимые библиотеки включены в AppImage. Сделайте файл
  исполняемым (`chmod +x Sopwith-*.AppImage`) и запустите его.
- **Debian / Ubuntu** (для обычного Linux-архива):

  ```sh
  sudo apt update
  sudo apt install libsdl2-2.0-0
  ```

- **Fedora:** `sudo dnf install SDL2`
- **Arch Linux:** `sudo pacman -S sdl2`
- **Calculinux на PicoCalc + Luckfox Lyra:**

  ```sh
  opkg update
  opkg install libsdl2-2.0-0
  ```

  Для самостоятельной сборки на Calculinux дополнительно установите
  `libsdl2-2.0-dev`.

## Сборка из исходников

Требуются компилятор C, GNU Make, Autotools и SDL2 development files. В Debian
и Ubuntu зависимости можно установить так:

```sh
sudo apt install build-essential autoconf automake libsdl2-dev
```

Сборка репозитория:

```sh
./autogen.sh
make
./src/sopwith
```

Подробности находятся в [doc/INSTALL](doc/INSTALL), а параметры запуска и
оригинальное управление — в [doc/sopwith.6](doc/sopwith.6).

## Лицензия и авторство

Meresyev Edition является производной работой SDL Sopwith и распространяется
на условиях GNU General Public License версии 2. См. [COPYING.md](COPYING.md) и
[AUTHORS](AUTHORS). Права на оригинальную игру и вклад участников сохраняются
за их авторами.

---

<a id="english"></a>

## English

**Sopwith — Meresyev Edition** is an expanded version of the classic SDL
Sopwith game. Losing an aircraft no longer necessarily ends a life: a surviving
pilot can continue on foot, return to base, use ground vehicles, and take to the
air again.

The project is based on [SDL Sopwith](https://github.com/fragglet/sdl-sopwith),
the modern port of David L. Clark's Sopwith, originally released by BMB
Compuscience Canada. The upstream README is preserved as
[README-UPSTREAM.md](README-UPSTREAM.md).

## Key features

- A pilot who can leave a crashed aircraft, walk, jump, and shoot.
- Five pilot lives; after death the pilot returns to base in a new aircraft.
- Hand grenades dropped by every defeated enemy soldier; the player starts with
  two grenades.
- Allied and enemy infantry with local detection, firearms, and grenades.
- Driveable cars and tanks. Tanks engage infantry, vehicles, aircraft, and
  buildings.
- Aircraft attack ground targets with guns and bombs, and a falling aircraft
  remains dangerous.
- Sound effects for weapons, grenades, vehicles, and aircraft engines.
- Unlimited replacement aircraft while pilot lives remain limited.

## Game modes

- **Vanilla — Novice / Expert** — classic gameplay on the standard map, with
  the original rules preserved as closely as possible.
- **Random Map — Novice / Expert** — procedurally generated terrain, buildings,
  armies, and vehicles.
- **Battlefield — Novice / Expert** — two armies fight continuously for
  territory. Reinforcements arrive roughly once per minute, infantry can rebuild
  destroyed buildings in 40 seconds, and a faction loses when it controls no
  territory. Player lives are unlimited in this mode.

The original modes retain their own AI and rules independently of Battlefield.

## Default controls

Keys can be reassigned in the options menu.

| Action | Key |
|---|---|
| Pull up / movement | `,` |
| Pull down / movement | `/` |
| Flip aircraft / jump / enter or leave a vehicle | `.` |
| Machine gun / sidearm / tank cannon | `Space` |
| Bomb / grenade | `B` |
| Accelerate | `X` |
| Decelerate | `Z` |
| Autopilot home | `H` |
| Sound | `S` |

## Prebuilt packages

[GitHub Releases](https://github.com/Jiablero/sopwith-meresyev-edition/releases)
provides packages for:

- Windows x86_64;
- Linux x86_64 (archive and AppImage);
- Calculinux ARMv7 for PicoCalc + Luckfox Lyra.

The PicoCalc build targets its 480×480 display. The original 320×200 game area
is scaled proportionally to 480×300 and centered, with a contrasting line marking
the upper flight boundary.

## Installing SDL2

- **Windows:** SDL2 is bundled in the ZIP next to `sopwith.exe`; no separate
  installation is required.
- **Linux AppImage:** the required libraries are bundled. Make the file
  executable (`chmod +x Sopwith-*.AppImage`) and run it.
- **Debian / Ubuntu** (for the regular Linux archive):

  ```sh
  sudo apt update
  sudo apt install libsdl2-2.0-0
  ```

- **Fedora:** `sudo dnf install SDL2`
- **Arch Linux:** `sudo pacman -S sdl2`
- **Calculinux on PicoCalc + Luckfox Lyra:**

  ```sh
  opkg update
  opkg install libsdl2-2.0-0
  ```

  Install `libsdl2-2.0-dev` as well when building from source on Calculinux.

## Building from source

A C compiler, GNU Make, Autotools, and SDL2 development files are required. On
Debian and Ubuntu, install the dependencies with:

```sh
sudo apt install build-essential autoconf automake libsdl2-dev
```

Build and run the repository with:

```sh
./autogen.sh
make
./src/sopwith
```

See [doc/INSTALL](doc/INSTALL) for more details and [doc/sopwith.6](doc/sopwith.6)
for command-line options and the original controls.

## License and credits

Meresyev Edition is a derivative of SDL Sopwith and is distributed under the
GNU General Public License version 2. See [COPYING.md](COPYING.md) and
[AUTHORS](AUTHORS). Copyright in the original game and individual contributions
remains with their respective authors.
