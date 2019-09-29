pipeline {
    agent any

    environment {
        PROJECT = 'sisl'
        CONAN_CHANNEL = 'testing'
        CONAN_USER = 'sisl'
        CONAN_PASS = credentials('CONAN_PASS')
    }

    stages {
        stage('Get Version') {
            steps {
                script {
                    TAG = sh(script: "grep -m 1 'version =' conanfile.py | awk '{print \$3}' | tr -d '\n' | tr -d '\"'", returnStdout: true)
                }
            }
        }

        stage('Build') {
            steps {
                sh "docker build --rm --build-arg BUILD_TYPE=debug --build-arg CONAN_USER=${CONAN_USER} --build-arg CONAN_PASS=${CONAN_PASS} --build-arg CONAN_CHANNEL=${CONAN_CHANNEL} -t ${PROJECT}-${GIT_COMMIT}-debug ."
                sh "docker build --rm --build-arg BUILD_TYPE=nosanitize --build-arg CONAN_USER=${CONAN_USER} --build-arg CONAN_PASS=${CONAN_PASS} --build-arg CONAN_CHANNEL=${CONAN_CHANNEL} -t ${PROJECT}-${GIT_COMMIT}-nosanitize ."
                sh "docker build --rm --build-arg CONAN_USER=${CONAN_USER} --build-arg CONAN_PASS=${CONAN_PASS} --build-arg CONAN_CHANNEL=${CONAN_CHANNEL} -t ${PROJECT}-${GIT_COMMIT} ."
            }
        }

        stage('Deploy') {
            when {
                branch "${CONAN_CHANNEL}/*"
            }
            steps {
                sh "docker run --rm ${PROJECT}-${GIT_COMMIT}"
                sh "docker run --rm ${PROJECT}-${GIT_COMMIT}-debug"
                sh "docker run --rm ${PROJECT}-${GIT_COMMIT}-nosanitize"
                slackSend channel: '#conan-pkgs', message: "*${PROJECT}/${TAG}@${CONAN_USER}/${CONAN_CHANNEL}* has been uploaded to conan repo."
            }
        }
    }

    post {
        always {
            sh "docker rmi -f ${PROJECT}-${GIT_COMMIT}"
            sh "docker rmi -f ${PROJECT}-${GIT_COMMIT}-debug"
            sh "docker rmi -f ${PROJECT}-${GIT_COMMIT}-nosanitize"
        }
    }
}
