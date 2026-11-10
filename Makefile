NAME = webserv

CXX = c++
CXXFLAGS = -Wall -Wextra -Werror -std=c++98

# Directory for object files
OBJDIR = obj

SRCS = src/main.cpp \
       src/webserv.cpp \
       src/Tokenizer.cpp \
       src/parser.cpp \
       src/Server.cpp \
       src/HttpRequest.cpp \
       src/Upload.cpp

# Store object files inside obj/
OBJS = $(patsubst src/%.cpp,$(OBJDIR)/%.o,$(SRCS))

INCLUDES = -I./hpp

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

# Create obj/ automatically before compilation
$(OBJDIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	# OLD: rm -f $(OBJS)
	rm -rf $(OBJDIR)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re