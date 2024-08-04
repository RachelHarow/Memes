Meme Generator - by Rachel Harow & Tal Gorodetzky
This project is a meme generator application built with C++ using various libraries for HTTP requests, JSON handling, image loading, and GUI creation. The application fetches meme templates from the Imgflip API and allows users to create custom memes.

Features:
Fetch meme templates from the Imgflip API.
Display memes in a table with sortable columns.
View meme images.
Create custom memes by entering text for the meme templates.
Save and display generated memes.
Responsive UI with ImGui for a user-friendly experience.


Libraries Used:
httplib: C++ HTTP library for making HTTP requests.
nlohmann/json: JSON library for handling JSON data.
stb_image: Image loading library for loading images from memory.
Dear ImGui: Immediate mode GUI library for the user interface.
DirectX9: Direct3D9 library for rendering images.
Win32 API: Windows API for handling window creation and events.


Requirements:
C++17 or higher
Visual Studio or a compatible C++ compiler
Internet connection for fetching meme templates

Setup and Build:
-Clone the repository:
git clone https://github.com/RachelHarow/Memes.git
cd Memes
-Download and place the required libraries in the appropriate directories. Ensure the following structure:
memes/
├── include/
│   ├── httplib.h
│   ├── json.hpp
│   ├── stb_image.h
│   ├── imgui.h
│   ├── imgui_impl_dx9.h
│   ├── imgui_impl_win32.h
├── src/
│   ├── main.cpp
│   └── ... (other source files)
├── libs/
│   ├── (any additional libraries)
└── ...

-Open the project in Visual Studio and ensure all include directories and library directories are correctly set.

-Build and run the project.

Usage:
Run the application. It will fetch the meme templates from the Imgflip API.
Use the search bar to find a specific meme template.
Click "See Image" to view the meme template.
Click "Create Meme" to enter text for the meme template.
Click "Generate Meme" to create the meme.
View the generated memes by clicking "Show Generated Memes".

License
This project is licensed under the MIT License. See the LICENSE file for details.

Acknowledgements:
Imgflip for providing the meme API.
Dear ImGui for the amazing GUI library.
nlohmann/json for the JSON library.
stb_image for the image loading library.
httplib for the HTTP library.

Feel free to contribute to this project by submitting issues or pull requests. Enjoy creating memes!
