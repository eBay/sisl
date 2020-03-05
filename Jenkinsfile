pipeline {
    agent any

    environment {
        CONAN_USER = 'sisl'
        CONAN_PASS = credentials('CONAN_PASS')
    }

    stages {
        stage('Get Version') {
            steps {
                script {
                    PROJECT = sh(script: "grep -m 1 'name =' conanfile.py | awk '{print \$3}' | tr -d '\n' | tr -d '\"'", returnStdout: true)
                    TAG = sh(script: "grep -m 1 'version =' conanfile.py | awk '{print \$3}' | tr -d '\n' | tr -d '\"'", returnStdout: true)
                    CONAN_CHANNEL = sh(script: "echo ${BRANCH_NAME} | sed -E 's,(\\w+).*,\\1,' | tr -d '\n'", returnStdout: true)
                }
            }
        }

        stage('Build') {
            steps {
                sh "docker build --rm --build-arg BUILD_TYPE=debug --build-arg CONAN_USER=${CONAN_USER} --build-arg CONAN_PASS=${CONAN_PASS} --build-arg CONAN_CHANNEL=${CONAN_CHANNEL} -t ${PROJECT}-${GIT_COMMIT}-debug ."
                sh "docker build --rm --build-arg CONAN_USER=${CONAN_USER} --build-arg CONAN_PASS=${CONAN_PASS} --build-arg CONAN_CHANNEL=${CONAN_CHANNEL} -t ${PROJECT}-${GIT_COMMIT}-release ."
            }
        }

        stage('Deploy') {
            steps {
                sh "docker run --rm ${PROJECT}-${GIT_COMMIT}-debug"
                sh "docker run --rm ${PROJECT}-${GIT_COMMIT}-release"
                slackSend channel: '#conan-pkgs', message: "*${PROJECT}/${TAG}@${CONAN_USER}/${CONAN_CHANNEL}* has been uploaded to conan repo."
            }
        }
    }

    post {
        always {
            sh "docker rmi -f ${PROJECT}-${GIT_COMMIT}-debug"
            sh "docker rmi -f ${PROJECT}-${GIT_COMMIT}-release"
        }
    }
}
