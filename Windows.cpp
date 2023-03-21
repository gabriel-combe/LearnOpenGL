#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "stb_image.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "Shader.h"
#include "camera.h"
#include "model.h"

#include <Unai_Math.h>

#include <iostream>
#include <WS2tcpip.h>
#include <thread>

#pragma comment (lib, "ws2_32.lib")

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow* window);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);

// settings
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

// camera
Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
bool firstMouse = true;

// timing
float deltaTime = 0.0f;	// time between current frame and last frame
float lastFrame = 0.0f;

// Quaternion
std::atomic<float> dataBMI085[7];
std::atomic<float> quat[4];
std::atomic<bool> retrieveQuat = true;
std::atomic<bool> newVal = false;

void dataReceiver()
{
	// Startup Winsock
	WSADATA data;
	WORD version = MAKEWORD(2, 2);
	int wsOk = WSAStartup(version, &data);
	if (wsOk != 0) {
		std::cout << "Can't start Winsock! " << wsOk << std::endl;
	}

	// Bind socket to ip address and port
	SOCKET in = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	sockaddr_in serverHint;
	serverHint.sin_addr.S_un.S_addr = INADDR_ANY;
	serverHint.sin_family = AF_INET;
	serverHint.sin_port = htons(54000);

	if (bind(in, (sockaddr*)&serverHint, sizeof(serverHint)) == SOCKET_ERROR) {
		std::cout << "Can't bind socket! " << WSAGetLastError() << std::endl;
	}

	sockaddr_in client;
	int clientLength = sizeof(client);
	ZeroMemory(&client, clientLength);

	char buf[7 * sizeof(float)];

	while (retrieveQuat.load()) {
		// receive quaternion rotation
		ZeroMemory(buf, 7 * sizeof(float));

		// Wait for message
		int bytesIn = recvfrom(in, buf, sizeof(buf), 0, (sockaddr*)&client, &clientLength);
		if (bytesIn == SOCKET_ERROR) {
			std::cout << "Error receiving from client " << WSAGetLastError() << std::endl;
			continue;
		}

		memcpy(&dataBMI085, buf, sizeof(buf));

		newVal.store(true);
	}

	// Close socket
	closesocket(in);

	// Shutdown winsock
	WSACleanup();
}

void UnscentedKalmanFilter() {
	Matrix<float> X(7, 1);
	Matrix<float> P(6, true);
	Matrix<float> Zk(6, 1);
	Matrix<float> G(3, 1);
	Matrix<float> Q(6, true);
	Matrix<float> R(6, true);

	float x[7] = {
		1.0f,
		0.0f,
		0.0f,
		0.0f,
		0.0f,
		0.0f,
		0.0f
	};

	P = P * 0.00001f;

	float q[36] = {
		0.00003f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.00003f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.00003f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.00000000003f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.00000000003f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.00000000003f
	};

	R = R * 0.005f;

	float g[3] = {
		0.0f,
		0.0f,
		9.81f
	};

	X.InitArray(x);
	Q.InitArray(q);

	while (true) {

		if (newVal.load()) {
			//std::cout << dataBMI085[0] << ", " << dataBMI085[1] << ", " << dataBMI085[2] << ", " << dataBMI085[3] << ", " << dataBMI085[4] << ", " << dataBMI085[5] << ", " << dataBMI085[6] << std::endl;

			float g[3] = {
				dataBMI085[0].load(),
				dataBMI085[1].load(),
				dataBMI085[2].load()
			};

			float z[6] = {
			dataBMI085[0].load(),
			dataBMI085[1].load(),
			dataBMI085[2].load(),
			dataBMI085[3].load(),
			dataBMI085[4].load(),
			dataBMI085[5].load()
			};

			Zk.InitArray(z);
			G.InitArray(g);

			UKF(X, P, Zk, G, dataBMI085[6].load(), Q, R);
			quat[0].store(X.GetElement(0, 0));
			quat[1].store(X.GetElement(1, 0));
			quat[2].store(X.GetElement(2, 0));
			quat[3].store(X.GetElement(3, 0));
			//std::cout << X.GetElement(0, 0) << ", " << X.GetElement(1, 0) << ", " << X.GetElement(2, 0) << ", " << X.GetElement(3, 0) << std::endl;

			newVal.store(false);
		}
	}
}

int Render()
{
	// Instantiate GLFW window
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	// Create window object
	GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "LearnOpenGL", NULL, NULL);
	if (window == NULL) {
		std::cout << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);

	// Register framebuffer_size_callback for resizing
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

	// Register mouse_callback and scroll_callback for mouse mouvement
	glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwSetCursorPosCallback(window, mouse_callback);
	glfwSetScrollCallback(window, scroll_callback);

	// Initializing GLAD before any call of OpenGL function
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		std::cout << "Failed to Initialize GLAD" << std::endl;
		return -1;
	}

	// tell stb_image.h to flip on the y-axis
	stbi_set_flip_vertically_on_load(true);

	// Enable Depth testing
	glEnable(GL_DEPTH_TEST);

	// build and compile our shader program
	Shader shader("shaders/model_loading.vs", "shaders/model_loading.fs");

	// Load Model
	Model ourModel("resources/objects/backpack/backpack.obj");

	// Allow drawing in wireframe mode
	//glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

	// Render Loop
	while (!glfwWindowShouldClose(window)) {
		// input
		processInput(window);

		// Clear the buffer
		glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Use the Program shader
		shader.use();

		// projection matrice
		glm::mat4 projection = glm::perspective(glm::radians(camera.Zoom), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);

		// view matrice
		glm::mat4 view = camera.GetViewMatrix();

		// set uniform variable from shader
		shader.setMat4("projection", projection);
		shader.setMat4("view", view);

		// render the loaded model
		glm::mat4 model = glm::mat4(1.0f);
		model = glm::translate(model, glm::vec3(0.0f, 0.0f, 0.0f));
		glm::mat4 rotation = glm::mat4_cast(glm::quat(quat[0], quat[1], quat[2], quat[3]));
		model = rotation * model;
		model = glm::scale(model, glm::vec3(1.0f, 1.0f, 1.0f));
		shader.setMat4("model", model);
		ourModel.Draw(shader);

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	// De-allocate ressources
	shader.programDelete();

	// clean/delete all GLFW's resources allocated
	glfwTerminate();

	retrieveQuat.store(false);

	return 0;
}

void show()
{
	while (true) {
		std::cout << quat[0] << ", " << quat[1] << ", " << quat[2] << ", " << quat[3] << std::endl;
	}
}

int main() 
{
	std::thread renderer(&Render);
	std::thread ukf(&UnscentedKalmanFilter);
	std::thread data(&dataReceiver);
	
	renderer.join();
	ukf.join();
	data.join();

	return 0;
}

// Function that can resize the window at any time
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
	glViewport(0, 0, width, height);
}

// Input Control
void processInput(GLFWwindow* window) {
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
		glfwSetWindowShouldClose(window, true);
	}

	const float cameraSpeed = 2.5f * deltaTime;
	if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
		camera.ProcessKeyboard(FORWARD, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
		camera.ProcessKeyboard(BACKWARD, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
		camera.ProcessKeyboard(LEFT, deltaTime);
	if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
		camera.ProcessKeyboard(RIGHT, deltaTime);
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
	if (firstMouse) {
		lastX = xpos;
		lastY = ypos;
		firstMouse = false;
	}

	float xoffset = xpos - lastX;
	float yoffset = lastY - ypos;

	lastX = xpos;
	lastY = ypos;

	camera.ProcessMouseMovement(xoffset, yoffset);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
	camera.ProcessMouseScroll(yoffset);
}